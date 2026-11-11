#include "Server.hpp"
#include "Channel.hpp"
#include "Client.hpp"
#include "Parser.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

/* ************************************************************************** */
/*                              상수 (방어용 한계값)                             */
/* ************************************************************************** */

static const size_t RECV_CHUNK_SIZE    = 512;          // RFC 1459: 한 메시지 최대 512바이트
static const size_t MAX_RECV_BUFFER    = 4096;         // "\r\n" 없이 쌓이는 입력 상한 (메모리 DoS 방지)
static const size_t MAX_SEND_BUFFER    = 1024 * 1024;  // 읽지 않는 클라이언트의 송신 큐 상한 (1MB)
static const int    MAX_PASS_ATTEMPTS  = 3;            // PASS 무차별 대입 방지
static const size_t NICK_MAX_LENGTH    = 30;
static const size_t USER_MAX_LENGTH    = 10;
static const size_t CHANNEL_MAX_LENGTH = 50;
static const size_t TOPIC_MAX_LENGTH   = 390;
static const size_t KEY_MAX_LENGTH     = 23;
static const size_t MAX_CHANNEL_LIMIT  = 10000;

/* ************************************************************************** */
/*                                시그널 처리                                    */
/* ************************************************************************** */

/**
 * 시그널 핸들러에서는 "async-signal-safe" 한 일만 해야 합니다.
 * (cout, new/delete, close 등은 금지) → 플래그만 세우고, 실제 정리는 메인 루프가 수행합니다.
 * sig_atomic_t + volatile: 핸들러와 메인 흐름 사이에서 원자적으로 읽고/쓰기 보장
 */
static volatile sig_atomic_t g_stopRequested = 0;

static void handleSignal(int signum)
{
	(void)signum;
	g_stopRequested = 1;
}

/* ************************************************************************** */
/*                              파일 내부 유틸 함수                              */
/* ************************************************************************** */

static std::string toString(unsigned long value)
{
	std::ostringstream oss;

	oss << value;
	return (oss.str());
}

static std::string toUpperCopy(const std::string& str)
{
	std::string result = str;

	for (size_t i = 0; i < result.size(); ++i)
	{
		if (result[i] >= 'a' && result[i] <= 'z')
		{
			result[i] = static_cast<char>(result[i] - 'a' + 'A');
		}
	}
	return (result);
}

/**
 * @brief "a,b,,c" → ["a", "b", "c"] (빈 토큰은 버림)
 */
static std::vector<std::string> splitByComma(const std::string& str)
{
	std::vector<std::string> result;
	std::string              token;
	std::istringstream       iss(str);

	while (std::getline(iss, token, ','))
	{
		if (!token.empty())
		{
			result.push_back(token);
		}
	}
	return (result);
}

static bool isChannelName(const std::string& name)
{
	return (!name.empty() && (name[0] == '#' || name[0] == '&'));
}

/**
 * @brief 채널명 유효성 검사 (RFC 2812 chanstring)
 *
 * '#' 또는 '&'로 시작, 2~50자, 공백/쉼표/콜론/제어문자(BELL 포함) 금지
 */
static bool isValidChannelName(const std::string& name)
{
	if (!isChannelName(name) || name.size() < 2 || name.size() > CHANNEL_MAX_LENGTH)
	{
		return (false);
	}
	for (size_t i = 1; i < name.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(name[i]);

		if (c <= 0x20 || c == ',' || c == ':' || c == 0x7F)
		{
			return (false);
		}
	}
	return (true);
}

static bool isNickSpecial(char c)
{
	return (c == '[' || c == ']' || c == '\\' || c == '`'
		|| c == '_' || c == '^' || c == '{' || c == '|' || c == '}');
}

/**
 * @brief 닉네임 유효성 검사 (RFC 2812 기반, 길이만 30자로 완화)
 *
 * 첫 글자: 영문자 또는 특수문자 []\`_^{|}
 * 나머지 : 영문자, 숫자, 특수문자, '-'
 * → '#'으로 시작하는 닉네임을 막아 채널명과 혼동되지 않게 합니다.
 */
static bool isValidNickname(const std::string& nick)
{
	if (nick.empty() || nick.size() > NICK_MAX_LENGTH)
	{
		return (false);
	}
	if (!std::isalpha(static_cast<unsigned char>(nick[0])) && !isNickSpecial(nick[0]))
	{
		return (false);
	}
	for (size_t i = 1; i < nick.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(nick[i]);

		if (!std::isalnum(c) && !isNickSpecial(nick[i]) && nick[i] != '-')
		{
			return (false);
		}
	}
	return (true);
}

/**
 * @brief 채널 키(+k) 유효성 검사: 1~23자, 쉼표/공백/제어문자 금지
 */
static bool isValidKey(const std::string& key)
{
	if (key.empty() || key.size() > KEY_MAX_LENGTH)
	{
		return (false);
	}
	for (size_t i = 0; i < key.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(key[i]);

		if (c <= 0x20 || c == ',' || c == 0x7F)
		{
			return (false);
		}
	}
	return (true);
}

/**
 * @brief +l 인자 파싱: 숫자로만 구성된 1 ~ MAX_CHANNEL_LIMIT 범위의 정수
 */
static bool parseLimit(const std::string& str, size_t& limit)
{
	if (str.empty() || str.size() > 6)
	{
		return (false);
	}
	for (size_t i = 0; i < str.size(); ++i)
	{
		if (str[i] < '0' || str[i] > '9')
		{
			return (false);
		}
	}

	std::istringstream iss(str);
	unsigned long      value = 0;

	iss >> value;
	if (iss.fail() || value == 0 || value > MAX_CHANNEL_LIMIT)
	{
		return (false);
	}
	limit = static_cast<size_t>(value);
	return (true);
}

/**
 * @brief 원본 라인에 trailing 파라미터(" :...")가 "존재"했는지 판별합니다.
 *
 * Command 구조체는 trailing을 문자열로만 담기 때문에
 * "TOPIC #a"(조회) 와 "TOPIC #a :"(토픽 삭제)를 구분할 수 없습니다.
 * Parser와 동일한 전처리(preprocessInput)를 거친 뒤 " :"가 있으면 trailing이 있는 것입니다.
 */
static bool hasTrailingParam(const std::string& line)
{
	std::string copy = line;

	removeLineEnding(copy);
	preprocessInput(copy);
	return (copy.find(" :") != std::string::npos);
}

/**
 * @brief Command(params + trailing)를 하나의 인자 벡터로 합칩니다.
 *
 * IRC 문법상 trailing은 "공백을 포함할 수 있는 마지막 파라미터"일 뿐이므로,
 * 핸들러에서는 args[0], args[1]... 로 일관되게 다루는 편이 훨씬 단순합니다.
 * 예) "PRIVMSG bob :hello world" → args = ["bob", "hello world"]
 */
static std::vector<std::string> buildArgs(const Command& cmd, const std::string& line)
{
	std::vector<std::string> args = cmd.params;

	if (hasTrailingParam(line))
	{
		args.push_back(cmd.trailing);
	}
	return (args);
}

/**
 * @brief 등록(PASS/NICK/USER) 완료 전에도 허용되는 명령인지 확인합니다.
 */
static bool isPreRegistrationCommand(const std::string& name)
{
	return (name == "PASS" || name == "NICK" || name == "USER" || name == "QUIT"
		|| name == "CAP" || name == "PING" || name == "PONG");
}

/**
 * @brief MODE 적용 결과 문자열을 압축 형태로 누적합니다.
 *
 * 예) +i, +k(secret), -l 순서로 적용되면 → modes="+ik-l", params=" secret"
 * 부호가 바뀔 때만 '+'/'-'를 새로 붙여서 "+i+k" 같은 중복 표기를 피합니다.
 */
static void appendModeChange(std::string& modes, std::string& params, char& lastSign,
							 char sign, char mode, const std::string& param)
{
	if (sign != lastSign)
	{
		modes += sign;
		lastSign = sign;
	}
	modes += mode;
	if (!param.empty())
	{
		params += " " + param;
	}
}

/* ************************************************************************** */
/*                              생성 / 소멸 / 실행                               */
/* ************************************************************************** */

Server::Server(int port, const std::string& password)
	: _port(port),
	  _password(password),
	  _serverFd(-1),
	  _isRunning(false),
	  _serverName("ft_irc"),
	  _version("ft_irc-1.0"),
	  _creationDate("")
{
	char        buffer[64];
	std::time_t now = std::time(NULL);

	std::memset(buffer, 0, sizeof(buffer));
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
	_creationDate = buffer;
	initCommandHandlers();
}

Server::~Server()
{
	cleanup();
}

/**
 * @brief SIGINT/SIGQUIT → 정상 종료 플래그, SIGPIPE → 무시
 *
 * [Defense 포인트] "SIGPIPE를 왜 무시하나요?"
 *  -> 상대가 이미 끊은 소켓에 send()하면 커널이 SIGPIPE를 보내고, 기본 동작은 "프로세스 종료"입니다.
 *     클라이언트 하나가 비정상 종료했다고 서버 전체가 죽으면 안 되므로 무시하고,
 *     대신 send()의 반환값(-1)으로 감지하여 해당 클라이언트만 정리합니다.
 *
 * sigaction의 sa_flags = 0 (SA_RESTART 미사용): poll()이 시그널로 깨어나 EINTR을 반환하게 하여
 * 메인 루프가 즉시 g_stopRequested를 확인할 수 있게 합니다.
 */
void Server::setupSignals()
{
	struct sigaction sa;

	std::memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handleSignal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	struct sigaction ignore;

	std::memset(&ignore, 0, sizeof(ignore));
	ignore.sa_handler = SIG_IGN;
	sigemptyset(&ignore.sa_mask);
	ignore.sa_flags = 0;
	sigaction(SIGPIPE, &ignore, NULL);
}

/**
 * @brief 커맨드 이름 → 핸들러 멤버 함수 포인터 테이블 등록
 *
 * 새 명령어를 추가할 때 이 테이블에 한 줄만 추가하면 됩니다. (Open-Closed)
 */
void Server::initCommandHandlers()
{
	// 인증 / 기본 통신 (sisung)
	_commandHandlers["CAP"]     = &Server::handleCap;
	_commandHandlers["PASS"]    = &Server::handlePass;
	_commandHandlers["NICK"]    = &Server::handleNick;
	_commandHandlers["USER"]    = &Server::handleUser;
	_commandHandlers["PING"]    = &Server::handlePing;
	_commandHandlers["PONG"]    = &Server::handlePong;
	_commandHandlers["QUIT"]    = &Server::handleQuit;
	_commandHandlers["PRIVMSG"] = &Server::handlePrivmsg;
	_commandHandlers["NOTICE"]  = &Server::handleNotice;
	_commandHandlers["WHO"]     = &Server::handleWho;

	// 채널 / 운영자 (jejeon)
	_commandHandlers["JOIN"]    = &Server::handleJoin;
	_commandHandlers["PART"]    = &Server::handlePart;
	_commandHandlers["TOPIC"]   = &Server::handleTopic;
	_commandHandlers["INVITE"]  = &Server::handleInvite;
	_commandHandlers["KICK"]    = &Server::handleKick;
	_commandHandlers["MODE"]    = &Server::handleMode;
}

void Server::initSocket()
{
	// 1. IPv4 TCP 소켓 생성
	_serverFd = socket(AF_INET, SOCK_STREAM, 0);
	if (_serverFd < 0)
	{
		throw std::runtime_error("Error: Failed to create server socket");
	}

	// 2. 소켓 재사용 옵션 (SO_REUSEADDR - 빠른 재시작 시 bind 에러 방지)
	//    [Defense] 서버를 껐다 바로 켜면 이전 연결이 TIME_WAIT 상태로 남아 "Address already in use"가 납니다.
	int opt = 1;
	if (setsockopt(_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
	{
		close(_serverFd);
		_serverFd = -1;
		throw std::runtime_error("Error: setsockopt(SO_REUSEADDR) failed");
	}

	// 3. 논블로킹 모드 설정 (서브젝트 필수 요구사항: F_SETFL, O_NONBLOCK 외 플래그 금지)
	if (fcntl(_serverFd, F_SETFL, O_NONBLOCK) < 0)
	{
		close(_serverFd);
		_serverFd = -1;
		throw std::runtime_error("Error: Failed to set non-blocking on server socket");
	}

	// 4. 주소 바인딩
	//    INADDR_ANY: 실제 IRC 클라이언트(다른 PC의 irssi 등)가 접속해야 하므로 모든 인터페이스에서 수신합니다.
	//    TODO(security): 로컬 테스트 전용으로 띄울 때는 INADDR_LOOPBACK(127.0.0.1)으로 제한하는 옵션을 고려
	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(_port);

	if (bind(_serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
		close(_serverFd);
		_serverFd = -1;
		throw std::runtime_error("Error: Failed to bind socket to port");
	}

	// 5. 연결 대기열 설정
	if (listen(_serverFd, SOMAXCONN) < 0)
	{
		close(_serverFd);
		_serverFd = -1;
		throw std::runtime_error("Error: Failed to listen on socket");
	}

	// 6. 서버 소켓을 poll 목록의 첫 번째 원소로 등록 (POLLIN = 새 연결 도착 감시)
	struct pollfd pfd;
	pfd.fd = _serverFd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	_pollFds.push_back(pfd);

	std::cout << "[INFO] Server listening on port " << _port << std::endl;
}

/**
 * @brief 메인 이벤트 루프: 프로그램 전체에서 유일한 poll() 호출 지점
 *
 * [Defense 포인트] "poll 타임아웃을 왜 -1로 두나요? Busy-waiting 아닌가요?"
 *  -> -1은 "이벤트가 생길 때까지 커널에서 잠들어라"는 뜻이라 CPU를 전혀 쓰지 않습니다.
 *     Busy-waiting은 오히려 타임아웃 0으로 계속 도는 경우이거나,
 *     보낼 데이터가 없는데 POLLOUT을 켜 두어 poll()이 계속 즉시 깨어나는 경우입니다.
 *     그래서 POLLOUT은 송신 큐에 데이터가 있을 때만 켭니다 (enablePollOut / disablePollOut).
 */
void Server::run()
{
	setupSignals();
	initSocket();
	_isRunning = true;

	while (_isRunning && !g_stopRequested)
	{
		int ready = poll(&_pollFds[0], static_cast<nfds_t>(_pollFds.size()), -1);

		if (ready < 0)
		{
			// 시그널(Ctrl+C 등)로 poll이 깨어난 경우: 루프 조건에서 g_stopRequested를 확인
			if (errno == EINTR)
			{
				continue;
			}
			std::cerr << "[ERROR] poll() failed: " << std::strerror(errno) << std::endl;
			break;
		}
		processPollEvents();
	}
	std::cout << std::endl << "[INFO] Shutting down server..." << std::endl;
	cleanup();
}

void Server::stop()
{
	_isRunning = false;
}

/**
 * @brief 모든 소켓 close + 모든 동적 할당 해제 (중복 호출 안전: 소멸자에서도 호출됨)
 */
void Server::cleanup()
{
	if (_serverFd < 0 && _clients.empty() && _channels.empty() && _pollFds.empty())
	{
		return;
	}

	// 1. 클라이언트 소켓 close 및 Client 객체 해제
	for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it)
	{
		close(it->first);
		delete it->second;
	}
	_clients.clear();

	// 2. Channel 객체 해제 (Channel은 Client*를 소유하지 않으므로 순서 무관)
	for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it)
	{
		delete it->second;
	}
	_channels.clear();

	// 3. 리스닝 소켓 close
	if (_serverFd >= 0)
	{
		close(_serverFd);
		_serverFd = -1;
	}
	_pollFds.clear();
	std::cout << "[INFO] Server cleanup finished." << std::endl;
}

/* ************************************************************************** */
/*                              poll() 이벤트 처리                               */
/* ************************************************************************** */

/**
 * @brief poll()이 돌려준 revents를 fd별로 분기 처리합니다.
 *
 * [이벤트 분기 규칙]
 *  1) listen 소켓 + POLLIN        → 새 연결 accept
 *  2) POLLERR | POLLNVAL          → 소켓 오류: 즉시 강제 종료 예약
 *  3) POLLIN                      → recv() (POLLHUP이 같이 와도 남은 데이터를 먼저 읽음)
 *  4) POLLHUP 단독                 → 상대가 완전히 끊음: 강제 종료 예약
 *  5) POLLOUT                     → 송신 큐 flush
 *
 * [Defense 포인트] "순회 도중 클라이언트가 끊기면 벡터 인덱스가 꼬이지 않나요?"
 *  -> 순회 중에는 _pollFds에서 절대 erase하지 않습니다. 종료할 클라이언트는 closing 플래그만 세우고,
 *     순회가 끝난 뒤 flushPendingDisconnects()에서 한꺼번에 제거합니다(지연 삭제).
 *     accept로 push_back되는 새 원소는 count를 미리 고정해 이번 순회에서는 건너뜁니다.
 *     (push_back은 재할당으로 참조를 무효화할 수 있으므로 반복자/참조 대신 인덱스로 접근)
 */
void Server::processPollEvents()
{
	size_t count = _pollFds.size();

	for (size_t i = 0; i < count; ++i)
	{
		int   fd = _pollFds[i].fd;
		short revents = _pollFds[i].revents;

		if (revents == 0)
		{
			continue;
		}
		_pollFds[i].revents = 0;

		if (fd == _serverFd)
		{
			if (revents & POLLIN)
			{
				handleNewConnection();
			}
			continue;
		}

		std::map<int, Client*>::iterator it = _clients.find(fd);
		if (it == _clients.end())
		{
			continue;
		}
		Client* client = it->second;

		if (revents & (POLLERR | POLLNVAL))
		{
			quitClient(client, "Connection error", false);
			continue;
		}
		if (revents & POLLIN)
		{
			handleClientRead(client);
		}
		else if (revents & POLLHUP)
		{
			quitClient(client, "Connection closed", false);
			continue;
		}
		if (revents & POLLOUT)
		{
			handleClientWrite(client);
		}
	}
	flushPendingDisconnects();
}

/**
 * @brief 새 연결 수락: accept → O_NONBLOCK → Client 생성 → pollfd(POLLIN) 등록
 *
 * poll이 POLLIN을 알려줬을 때만 accept하므로 블로킹되지 않습니다.
 * 한 번에 하나만 accept하고, 대기 연결이 더 있으면 다음 poll()이 다시 알려줍니다.
 */
void Server::handleNewConnection()
{
	struct sockaddr_in addr;
	socklen_t          addrLen = sizeof(addr);

	std::memset(&addr, 0, sizeof(addr));
	int clientFd = accept(_serverFd, (struct sockaddr*)&addr, &addrLen);
	if (clientFd < 0)
	{
		return;
	}

	// accept된 소켓도 반드시 논블로킹으로 (recv/send가 절대 멈추지 않도록)
	if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0)
	{
		close(clientFd);
		return;
	}

	Client* client = NULL;
	try
	{
		client = new Client(clientFd, inet_ntoa(addr.sin_addr));
		_clients[clientFd] = client;

		struct pollfd pfd;
		pfd.fd = clientFd;
		pfd.events = POLLIN;  // 처음엔 읽기만 감시 (보낼 게 생기면 POLLOUT 추가)
		pfd.revents = 0;
		_pollFds.push_back(pfd);
	}
	catch (const std::exception& e)
	{
		_clients.erase(clientFd);
		delete client;
		close(clientFd);
		std::cerr << "[ERROR] Failed to register client: " << e.what() << std::endl;
		return;
	}
	std::cout << "[INFO] New connection: fd=" << clientFd
			  << " host=" << client->getHostname() << std::endl;
}

/**
 * @brief POLLIN 처리: recv → 버퍼 누적 → "\r\n" 단위 라인 추출 → 명령 실행
 *
 * [Partial packet 파이프라인]
 *   (1) recv()로 받은 만큼 _recvBuffer에 append
 *   (2) getLineFromBuffer()가 "\r\n"을 찾으면 그 앞까지를 line으로 잘라내고 버퍼에서 제거
 *   (3) "\r\n"이 아직 없으면 false → 남은 조각은 다음 recv까지 버퍼에서 대기
 *
 *   nc -C 에서 "PA" [Ctrl+D] "SS pw" [Ctrl+D] [Enter] 순으로 보내면
 *   버퍼: "PA" → "PASS pw" → "PASS pw\r\n" → 비로소 "PASS pw" 한 줄 처리
 *
 * [Defense 포인트] recv() 반환값
 *   > 0 : 받은 바이트 수
 *   = 0 : 상대가 연결을 정상 종료(EOF, FIN 수신)
 *   < 0 : 오류 → 서브젝트상 errno로 재시도 분기하지 않고 연결을 정리
 */
void Server::handleClientRead(Client* client)
{
	char    buffer[RECV_CHUNK_SIZE];
	ssize_t bytes = recv(client->getFd(), buffer, sizeof(buffer), 0);

	if (bytes <= 0)
	{
		// EOF: 이미 처리된 응답은 최대한 전달(graceful), 오류는 즉시 정리
		quitClient(client, bytes == 0 ? "Remote host closed the connection" : "Read error", bytes == 0);
		return;
	}
	if (client->isClosing())
	{
		return;
	}

	client->appendToRecvBuffer(buffer, static_cast<size_t>(bytes));

	std::string line;
	while (!client->isClosing() && getLineFromBuffer(client->getRecvBuffer(), line))
	{
		if (line.empty())
		{
			continue;
		}
		processLine(client, line);
	}

	// "\r\n" 없이 거대한 데이터만 계속 보내는 클라이언트 → 메모리 고갈 공격 방지
	if (!client->isClosing() && client->getRecvBuffer().size() > MAX_RECV_BUFFER)
	{
		quitClient(client, "Input buffer exceeded", true);
	}
}

/**
 * @brief POLLOUT 처리: 송신 큐를 send() 하고, 비면 POLLOUT을 끕니다.
 *
 * [POLLOUT 토글 원리]
 *  - 소켓은 거의 항상 "쓰기 가능" 상태입니다. POLLOUT을 항상 켜 두면 poll()이 매번 즉시 반환되어
 *    CPU 100%를 먹는 Busy-waiting이 됩니다.
 *  - 그래서 sendToClient()가 큐에 데이터를 넣을 때 POLLOUT ON,
 *    여기서 큐를 다 비우면 POLLOUT OFF 합니다.
 *  - send()가 일부만 보낸 경우(partial write) POLLOUT을 유지해 다음 루프에서 이어 보냅니다.
 */
void Server::handleClientWrite(Client* client)
{
	int fd = client->getFd();

	if (!client->hasPendingSend())
	{
		disablePollOut(fd);
		return;
	}

	const std::string& out = client->getSendBuffer();
	ssize_t            sent = send(fd, out.data(), out.size(), 0);

	if (sent < 0)
	{
		quitClient(client, "Write error", false);
		return;
	}
	client->consumeSendBuffer(static_cast<size_t>(sent));
	if (!client->hasPendingSend())
	{
		disablePollOut(fd);
	}
}

/**
 * @brief 클라이언트를 실제로 제거합니다: 채널 탈퇴 → close → pollfd 제거 → delete
 *
 * 반드시 poll 순회가 끝난 뒤(flushPendingDisconnects)에서만 호출됩니다.
 */
void Server::disconnectClient(int fd)
{
	std::map<int, Client*>::iterator it = _clients.find(fd);
	if (it == _clients.end())
	{
		return;
	}
	Client* client = it->second;

	// 아직 채널 동료들에게 QUIT을 알리지 않았다면(송신 큐 폭주 등) 여기서 처리
	announceQuit(client, "Connection closed");

	close(fd);
	for (std::vector<struct pollfd>::iterator pit = _pollFds.begin(); pit != _pollFds.end(); ++pit)
	{
		if (pit->fd == fd)
		{
			_pollFds.erase(pit);
			break;
		}
	}
	std::cout << "[INFO] Client disconnected: fd=" << fd
			  << " nick=" << (client->getNickname().empty() ? "*" : client->getNickname()) << std::endl;
	delete client;
	_clients.erase(it);
}

/**
 * @brief 종료 예약(closing) 상태이면서 송신 큐가 빈 클라이언트를 일괄 제거합니다. (지연 삭제)
 *
 * QUIT 시 "ERROR :Closing Link" 메시지를 먼저 전달한 뒤 닫기 위해,
 * 큐가 남아 있으면 POLLOUT으로 다 보낼 때까지 기다립니다.
 * 제거 과정(announceQuit)에서 다른 클라이언트가 새로 closing이 될 수 있으므로 반복합니다.
 */
void Server::flushPendingDisconnects()
{
	while (true)
	{
		std::vector<int> targets;

		for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it)
		{
			if (it->second->isClosing() && !it->second->hasPendingSend())
			{
				targets.push_back(it->first);
			}
		}
		if (targets.empty())
		{
			break;
		}
		for (size_t i = 0; i < targets.size(); ++i)
		{
			disconnectClient(targets[i]);
		}
	}
}

/* ************************************************************************** */
/*                              pollfd 이벤트 토글                               */
/* ************************************************************************** */

struct pollfd* Server::findPollFd(int fd)
{
	for (size_t i = 0; i < _pollFds.size(); ++i)
	{
		if (_pollFds[i].fd == fd)
		{
			return (&_pollFds[i]);
		}
	}
	return (NULL);
}

void Server::enablePollOut(int fd)
{
	struct pollfd* pfd = findPollFd(fd);

	if (pfd != NULL)
	{
		pfd->events |= POLLOUT;   // 비트 OR: 기존 POLLIN은 유지한 채 POLLOUT만 켬
	}
}

void Server::disablePollOut(int fd)
{
	struct pollfd* pfd = findPollFd(fd);

	if (pfd != NULL)
	{
		pfd->events &= ~POLLOUT;  // 비트 AND NOT: POLLOUT 비트만 끔
	}
}

/**
 * @brief 종료 예약된 클라이언트는 더 읽지 않습니다. (EOF 상태에서 POLLIN 무한 발생 방지)
 */
void Server::disablePollIn(int fd)
{
	struct pollfd* pfd = findPollFd(fd);

	if (pfd != NULL)
	{
		pfd->events &= ~POLLIN;
	}
}

/* ************************************************************************** */
/*                              송신 (큐잉 전용)                                 */
/* ************************************************************************** */

/**
 * @brief 메시지를 클라이언트 송신 큐에 넣고 POLLOUT을 켭니다. (여기서 send() 호출 절대 금지)
 *
 * 모든 핸들러는 이 함수만 통해 응답하므로, 실제 소켓 쓰기는 오직 poll()의 POLLOUT 이벤트에서만 일어납니다.
 */
void Server::sendToClient(Client* client, const std::string& message)
{
	if (client->isClosing())
	{
		return;
	}
	if (client->getSendBuffer().size() + message.size() + 2 > MAX_SEND_BUFFER)
	{
		// 데이터를 읽지 않는 클라이언트(SendQ 폭주): 메모리 보호를 위해 강제 종료 예약
		// (여기서 채널 조작을 하면 브로드캐스트 순회 중 컨테이너가 바뀌므로 플래그만 세움)
		client->clearSendBuffer();
		client->setClosing(true);
		disablePollIn(client->getFd());
		return;
	}
	client->queueMessage(message + "\r\n");
	enablePollOut(client->getFd());
}

/**
 * @brief 서버 numeric reply: ":ft_irc <code> <nick|*> <params>"
 */
void Server::sendNumeric(Client* client, const std::string& code, const std::string& params)
{
	std::string target = client->getNickname().empty() ? "*" : client->getNickname();

	sendToClient(client, ":" + _serverName + " " + code + " " + target + " " + params);
}

/**
 * @brief 채널 전체에 메시지를 큐잉합니다. (exclude가 있으면 그 사람은 제외: PRIVMSG 본인 제외용)
 */
void Server::broadcastToChannel(Channel* channel, const std::string& message, Client* exclude)
{
	const std::vector<Client*>& members = channel->getMembers();

	for (size_t i = 0; i < members.size(); ++i)
	{
		if (members[i] != exclude)
		{
			sendToClient(members[i], message);
		}
	}
}

/**
 * @brief client와 채널을 하나라도 공유하는 모든 사람에게 "한 번씩만" 전송합니다. (NICK 변경 등)
 *
 * std::set으로 중복 제거: 같은 사람과 채널 3개를 공유해도 메시지는 1번만 갑니다.
 */
void Server::broadcastToPeers(Client* client, const std::string& message, bool includeSelf)
{
	std::set<Client*> recipients;

	for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it)
	{
		if (!it->second->hasMember(client))
		{
			continue;
		}
		const std::vector<Client*>& members = it->second->getMembers();
		for (size_t i = 0; i < members.size(); ++i)
		{
			recipients.insert(members[i]);
		}
	}
	if (includeSelf)
	{
		recipients.insert(client);
	}
	else
	{
		recipients.erase(client);
	}
	for (std::set<Client*>::iterator it = recipients.begin(); it != recipients.end(); ++it)
	{
		sendToClient(*it, message);
	}
}

/* ************************************************************************** */
/*                              명령 처리 공통                                   */
/* ************************************************************************** */

/**
 * @brief 완성된 한 줄을 파싱하여 등록 상태 검사 후 핸들러로 디스패치합니다.
 */
void Server::processLine(Client* client, const std::string& line)
{
	Command cmd = commandParser(line);

	if (cmd.name.empty())
	{
		return;
	}

	// 로그에는 명령 이름만 남깁니다 (PASS 비밀번호, 개인 메시지 내용 노출 방지)
	std::cout << "[CMD] fd=" << client->getFd()
			  << " nick=" << (client->getNickname().empty() ? "*" : client->getNickname())
			  << " " << cmd.name << std::endl;

	std::vector<std::string> args = buildArgs(cmd, line);

	std::map<std::string, CommandHandler>::iterator it = _commandHandlers.find(cmd.name);

	// [Defense] PASS/NICK/USER 완료 전에는 채널/메시지 명령 거부 (451 ERR_NOTREGISTERED)
	if (!client->isRegistered() && !isPreRegistrationCommand(cmd.name))
	{
		sendNumeric(client, "451", ":You have not registered");
		return;
	}
	if (it == _commandHandlers.end())
	{
		sendNumeric(client, "421", cmd.name + " :Unknown command");
		return;
	}
	(this->*(it->second))(client, args);
}

/**
 * @brief PASS + NICK + USER 3가지가 "순서 무관하게" 모두 충족되면 등록을 완료합니다.
 *
 * PASS/NICK/USER 핸들러가 각각 끝날 때마다 호출되므로, 마지막 조건이 채워지는 순간 환영 메시지가 나갑니다.
 */
void Server::tryCompleteRegistration(Client* client)
{
	if (client->isRegistered() || !client->isPassAccepted()
		|| client->getNickname().empty() || client->getUsername().empty())
	{
		return;
	}
	client->setRegistered(true);

	const std::string& nick = client->getNickname();

	sendNumeric(client, "001", ":Welcome to the ft_irc Network, " + client->getPrefix());
	sendNumeric(client, "002", ":Your host is " + _serverName + ", running version " + _version);
	sendNumeric(client, "003", ":This server was created " + _creationDate);
	sendNumeric(client, "004", _serverName + " " + _version + " o itkol");
	sendNumeric(client, "005", "CHANTYPES=#& PREFIX=(o)@ CHANMODES=,k,l,it NICKLEN="
		+ toString(NICK_MAX_LENGTH) + " CHANNELLEN=" + toString(CHANNEL_MAX_LENGTH)
		+ " TOPICLEN=" + toString(TOPIC_MAX_LENGTH) + " CASEMAPPING=rfc1459 :are supported by this server");
	sendNumeric(client, "422", ":MOTD File is missing");
	std::cout << "[INFO] Registered: fd=" << client->getFd() << " nick=" << nick << std::endl;
}

/**
 * @brief 클라이언트 종료를 "예약"합니다. 실제 close/delete는 flushPendingDisconnects()에서.
 *
 * @param graceful true  → "ERROR :Closing Link" 를 큐잉하고 큐를 다 보낸 뒤 닫음 (QUIT 명령)
 *                 false → 큐를 버리고 즉시 닫음 (소켓 오류, 상대가 이미 끊음)
 */
void Server::quitClient(Client* client, const std::string& reason, bool graceful)
{
	announceQuit(client, reason);
	if (client->isClosing())
	{
		if (!graceful)
		{
			client->clearSendBuffer();
		}
		return;
	}
	if (graceful)
	{
		sendToClient(client, "ERROR :Closing Link: " + client->getHostname() + " (" + reason + ")");
	}
	else
	{
		client->clearSendBuffer();
	}
	client->setClosing(true);
	disablePollIn(client->getFd());
}

/**
 * @brief 채널 동료들에게 QUIT을 1번씩 알리고, 모든 채널에서 탈퇴시킵니다. (빈 채널은 소멸)
 */
void Server::announceQuit(Client* client, const std::string& reason)
{
	if (client->isQuitAnnounced())
	{
		return;
	}
	client->setQuitAnnounced(true);

	std::string       message = ":" + client->getPrefix() + " QUIT :" + reason;
	std::set<Client*> recipients;

	std::map<std::string, Channel*>::iterator it = _channels.begin();
	while (it != _channels.end())
	{
		Channel* channel = it->second;

		if (!channel->hasMember(client))
		{
			++it;
			continue;
		}
		const std::vector<Client*>& members = channel->getMembers();
		for (size_t i = 0; i < members.size(); ++i)
		{
			if (members[i] != client)
			{
				recipients.insert(members[i]);
			}
		}
		channel->removeMember(client);
		if (channel->isEmpty())
		{
			delete channel;
			_channels.erase(it++);  // erase 전에 반복자를 전진시켜 무효화 방지 (C++98 관용구)
		}
		else
		{
			++it;
		}
	}
	for (std::set<Client*>::iterator rit = recipients.begin(); rit != recipients.end(); ++rit)
	{
		sendToClient(*rit, message);
	}
}

/* ************************************************************************** */
/*                              조회 / 채널 유틸                                 */
/* ************************************************************************** */

Client* Server::findClientByNick(const std::string& nickname) const
{
	std::string target = ircToLower(nickname);

	for (std::map<int, Client*>::const_iterator it = _clients.begin(); it != _clients.end(); ++it)
	{
		const std::string& nick = it->second->getNickname();

		if (!nick.empty() && ircToLower(nick) == target)
		{
			return (it->second);
		}
	}
	return (NULL);
}

Channel* Server::findChannel(const std::string& name) const
{
	std::map<std::string, Channel*>::const_iterator it = _channels.find(ircToLower(name));

	if (it == _channels.end())
	{
		return (NULL);
	}
	return (it->second);
}

/**
 * @brief 멤버가 0명이 된 채널을 맵에서 제거하고 메모리를 해제합니다.
 *
 * 호출 후 channel 포인터는 dangling이 될 수 있으므로 더 이상 사용하면 안 됩니다.
 */
void Server::removeChannelIfEmpty(Channel* channel)
{
	if (!channel->isEmpty())
	{
		return;
	}
	_channels.erase(ircToLower(channel->getName()));
	delete channel;
}

void Server::sendTopicReply(Client* client, Channel* channel)
{
	if (channel->getTopic().empty())
	{
		sendNumeric(client, "331", channel->getName() + " :No topic is set");
		return;
	}
	sendNumeric(client, "332", channel->getName() + " :" + channel->getTopic());
	sendNumeric(client, "333", channel->getName() + " " + channel->getTopicSetter() + " "
		+ toString(static_cast<unsigned long>(channel->getTopicTime())));
}

void Server::sendNamesReply(Client* client, Channel* channel)
{
	sendNumeric(client, "353", "= " + channel->getName() + " :" + channel->getNamesList());
	sendNumeric(client, "366", channel->getName() + " :End of /NAMES list");
}

void Server::sendWhoReply(Client* client, const std::string& channelName, Client* target, bool isOp)
{
	sendNumeric(client, "352", channelName + " " + target->getUsername() + " " + target->getHostname()
		+ " " + _serverName + " " + target->getNickname() + " H" + (isOp ? "@" : "")
		+ " :0 " + target->getRealname());
}

/* ************************************************************************** */
/*                     커맨드 핸들러: 인증 / 기본 통신 (sisung)                    */
/* ************************************************************************** */

/**
 * @brief CAP: 최신 클라이언트(irssi, HexChat)의 capability 협상에 "지원 기능 없음"으로 응답
 */
void Server::handleCap(Client* client, const std::vector<std::string>& args)
{
	if (args.empty())
	{
		return;
	}

	std::string sub = toUpperCopy(args[0]);
	std::string target = client->getNickname().empty() ? "*" : client->getNickname();

	if (sub == "LS")
	{
		sendToClient(client, ":" + _serverName + " CAP " + target + " LS :");
	}
	else if (sub == "REQ")
	{
		sendToClient(client, ":" + _serverName + " CAP " + target + " NAK :"
			+ (args.size() > 1 ? args[1] : ""));
	}
}

/**
 * @brief PASS <password>
 *
 * - 등록 후 재시도: 462 ERR_ALREADYREGISTRED
 * - 인자 없음      : 461 ERR_NEEDMOREPARAMS
 * - 불일치         : 464 ERR_PASSWDMISMATCH (3회 실패 시 연결 종료 → 무차별 대입 방지)
 */
void Server::handlePass(Client* client, const std::vector<std::string>& args)
{
	if (client->isRegistered())
	{
		sendNumeric(client, "462", ":You may not reregister");
		return;
	}
	if (args.empty() || args[0].empty())
	{
		sendNumeric(client, "461", "PASS :Not enough parameters");
		return;
	}
	if (args[0] != _password)
	{
		client->setPassAccepted(false);
		client->increasePassAttempts();
		sendNumeric(client, "464", ":Password incorrect");
		if (client->getPassAttempts() >= MAX_PASS_ATTEMPTS)
		{
			quitClient(client, "Too many password attempts", true);
		}
		return;
	}
	client->setPassAccepted(true);
	tryCompleteRegistration(client);
}

/**
 * @brief NICK <nickname>
 *
 * - 431 ERR_NONICKNAMEGIVEN, 432 ERR_ERRONEUSNICKNAME, 433 ERR_NICKNAMEINUSE
 * - 등록 후 변경 시 ":old!user@host NICK :new" 를 본인 + 채널 동료에게 브로드캐스트
 */
void Server::handleNick(Client* client, const std::vector<std::string>& args)
{
	if (args.empty() || args[0].empty())
	{
		sendNumeric(client, "431", ":No nickname given");
		return;
	}

	const std::string& nick = args[0];

	if (!isValidNickname(nick))
	{
		sendNumeric(client, "432", nick + " :Erroneous nickname");
		return;
	}

	Client* owner = findClientByNick(nick);
	if (owner != NULL && owner != client)
	{
		sendNumeric(client, "433", nick + " :Nickname is already in use");
		return;
	}
	if (nick == client->getNickname())
	{
		return;
	}

	if (!client->isRegistered())
	{
		client->setNickname(nick);
		tryCompleteRegistration(client);
		return;
	}

	std::string oldNick = client->getNickname();

	broadcastToPeers(client, ":" + client->getPrefix() + " NICK :" + nick, true);
	for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it)
	{
		it->second->renameInvite(oldNick, nick);
	}
	client->setNickname(nick);
}

/**
 * @brief USER <username> <mode> <unused> :<realname>
 */
void Server::handleUser(Client* client, const std::vector<std::string>& args)
{
	if (client->isRegistered())
	{
		sendNumeric(client, "462", ":You may not reregister");
		return;
	}
	if (args.size() < 4 || args[0].empty())
	{
		sendNumeric(client, "461", "USER :Not enough parameters");
		return;
	}
	client->setUsername(args[0].substr(0, USER_MAX_LENGTH));
	client->setRealname(args[3]);
	tryCompleteRegistration(client);
}

/**
 * @brief PING <token> → PONG (클라이언트의 연결 생존 확인에 응답)
 */
void Server::handlePing(Client* client, const std::vector<std::string>& args)
{
	if (args.empty() || args[0].empty())
	{
		sendNumeric(client, "409", ":No origin specified");
		return;
	}
	sendToClient(client, ":" + _serverName + " PONG " + _serverName + " :" + args[0]);
}

void Server::handlePong(Client* client, const std::vector<std::string>& args)
{
	(void)client;
	(void)args;
}

/**
 * @brief QUIT [:<reason>] → 채널 동료에게 QUIT 브로드캐스트 후 ERROR 전송, 큐 소진 뒤 close
 */
void Server::handleQuit(Client* client, const std::vector<std::string>& args)
{
	std::string reason = "Client Quit";

	if (!args.empty() && !args[0].empty())
	{
		reason = "Quit: " + args[0];
	}
	quitClient(client, reason, true);
}

void Server::handlePrivmsg(Client* client, const std::vector<std::string>& args)
{
	relayMessage(client, args, "PRIVMSG", true);
}

/**
 * @brief NOTICE: PRIVMSG와 동일하지만 RFC상 어떤 에러 응답도 보내면 안 됨 (자동응답 루프 방지)
 */
void Server::handleNotice(Client* client, const std::vector<std::string>& args)
{
	relayMessage(client, args, "NOTICE", false);
}

/**
 * @brief PRIVMSG/NOTICE <target>{,<target>} :<text>
 *
 * - 채널 대상: 멤버만 전송 가능(404), 본인을 제외한 모든 멤버에게 브로드캐스트
 * - 유저 대상: 1:1 귓속말
 * - 411 ERR_NORECIPIENT, 412 ERR_NOTEXTTOSEND, 401 ERR_NOSUCHNICK, 404 ERR_CANNOTSENDTOCHAN
 */
void Server::relayMessage(Client* client, const std::vector<std::string>& args,
						  const std::string& command, bool sendErrors)
{
	if (args.empty() || args[0].empty())
	{
		if (sendErrors)
		{
			sendNumeric(client, "411", ":No recipient given (" + command + ")");
		}
		return;
	}
	if (args.size() < 2 || args[1].empty())
	{
		if (sendErrors)
		{
			sendNumeric(client, "412", ":No text to send");
		}
		return;
	}

	const std::string&       text = args[1];
	std::vector<std::string> targets = splitByComma(args[0]);

	for (size_t i = 0; i < targets.size(); ++i)
	{
		const std::string& target = targets[i];

		if (isChannelName(target))
		{
			Channel* channel = findChannel(target);

			if (channel == NULL)
			{
				if (sendErrors)
				{
					sendNumeric(client, "401", target + " :No such nick/channel");
				}
				continue;
			}
			if (!channel->hasMember(client))
			{
				if (sendErrors)
				{
					sendNumeric(client, "404", channel->getName() + " :Cannot send to channel");
				}
				continue;
			}
			broadcastToChannel(channel, ":" + client->getPrefix() + " " + command + " "
				+ channel->getName() + " :" + text, client);
			continue;
		}

		Client* receiver = findClientByNick(target);
		if (receiver == NULL || !receiver->isRegistered())
		{
			if (sendErrors)
			{
				sendNumeric(client, "401", target + " :No such nick/channel");
			}
			continue;
		}
		sendToClient(receiver, ":" + client->getPrefix() + " " + command + " "
			+ receiver->getNickname() + " :" + text);
	}
}

/**
 * @brief WHO <channel|nick>: irssi 등이 채널 입장 직후 자동으로 보내는 조회에 대한 최소 응답
 */
void Server::handleWho(Client* client, const std::vector<std::string>& args)
{
	std::string mask = args.empty() ? "*" : args[0];

	if (isChannelName(mask))
	{
		Channel* channel = findChannel(mask);

		if (channel != NULL && channel->hasMember(client))
		{
			const std::vector<Client*>& members = channel->getMembers();

			for (size_t i = 0; i < members.size(); ++i)
			{
				sendWhoReply(client, channel->getName(), members[i], channel->isOperator(members[i]));
			}
		}
	}
	else if (!args.empty())
	{
		Client* target = findClientByNick(mask);

		if (target != NULL && target->isRegistered())
		{
			sendWhoReply(client, "*", target, false);
		}
	}
	sendNumeric(client, "315", mask + " :End of WHO list");
}

/* ************************************************************************** */
/*                     커맨드 핸들러: 채널 / 운영자 (jejeon)                       */
/* ************************************************************************** */

/**
 * @brief JOIN <channel>{,<channel>} [<key>{,<key>}]   /   JOIN 0 (모든 채널 퇴장)
 *
 * 채널 목록과 키 목록은 같은 인덱스끼리 짝지어집니다.
 * 예) JOIN #a,#b keyA → #a는 "keyA", #b는 키 없음
 */
void Server::handleJoin(Client* client, const std::vector<std::string>& args)
{
	if (args.empty() || args[0].empty())
	{
		sendNumeric(client, "461", "JOIN :Not enough parameters");
		return;
	}
	if (args[0] == "0")
	{
		partAllChannels(client);
		return;
	}

	std::vector<std::string> names = splitByComma(args[0]);
	std::vector<std::string> keys;

	if (args.size() > 1)
	{
		keys = splitByComma(args[1]);
	}
	for (size_t i = 0; i < names.size(); ++i)
	{
		joinSingleChannel(client, names[i], i < keys.size() ? keys[i] : "");
	}
}

/**
 * @brief 채널 1개 입장 처리
 *
 * [검사 순서] (기존 채널일 때)
 *  1) +i : 초대 목록에 없으면 473 ERR_INVITEONLYCHAN
 *  2) +k : 키 불일치 시 475 ERR_BADCHANNELKEY
 *  3) +l : 인원 초과 시 471 ERR_CHANNELISFULL
 *
 * [성공 시 응답]
 *  JOIN 브로드캐스트(본인 포함) → 332/333 또는 331 (토픽) → 353 (멤버 목록) → 366 (목록 끝)
 */
void Server::joinSingleChannel(Client* client, const std::string& name, const std::string& key)
{
	if (!isValidChannelName(name))
	{
		sendNumeric(client, "476", name + " :Bad Channel Mask");
		return;
	}

	Channel* channel = findChannel(name);
	bool     created = false;

	if (channel == NULL)
	{
		// 신규 채널 생성: 첫 입장자가 운영자(@)
		channel = new Channel(name);
		_channels[ircToLower(name)] = channel;
		created = true;
	}
	else
	{
		if (channel->hasMember(client))
		{
			return;
		}
		if (channel->isInviteOnly() && !channel->isInvited(client->getNickname()))
		{
			sendNumeric(client, "473", channel->getName() + " :Cannot join channel (+i)");
			return;
		}
		if (channel->hasKey() && key != channel->getKey())
		{
			sendNumeric(client, "475", channel->getName() + " :Cannot join channel (+k)");
			return;
		}
		if (channel->isFull())
		{
			sendNumeric(client, "471", channel->getName() + " :Cannot join channel (+l)");
			return;
		}
	}

	channel->addMember(client);
	if (created)
	{
		channel->addOperator(client);
	}
	channel->removeInvite(client->getNickname());  // 초대는 1회용

	broadcastToChannel(channel, ":" + client->getPrefix() + " JOIN " + channel->getName(), NULL);
	sendTopicReply(client, channel);
	sendNamesReply(client, channel);
}

/**
 * @brief JOIN 0: 가입한 모든 채널에서 PART
 *
 * 순회 중 채널이 삭제될 수 있으므로 대상 목록을 먼저 복사한 뒤 처리합니다.
 */
void Server::partAllChannels(Client* client)
{
	std::vector<Channel*> joined;

	for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it)
	{
		if (it->second->hasMember(client))
		{
			joined.push_back(it->second);
		}
	}
	for (size_t i = 0; i < joined.size(); ++i)
	{
		broadcastToChannel(joined[i], ":" + client->getPrefix() + " PART " + joined[i]->getName(), NULL);
		joined[i]->removeMember(client);
		removeChannelIfEmpty(joined[i]);
	}
}

/**
 * @brief PART <channel>{,<channel>} [:<reason>]
 *
 * PART 메시지는 본인 포함 전체에 브로드캐스트한 "뒤" 멤버에서 제거합니다.
 * (먼저 제거하면 본인이 자기 PART 메시지를 못 받아 클라이언트 창이 닫히지 않음)
 */
void Server::handlePart(Client* client, const std::vector<std::string>& args)
{
	if (args.empty() || args[0].empty())
	{
		sendNumeric(client, "461", "PART :Not enough parameters");
		return;
	}

	std::string              reason = args.size() > 1 ? args[1] : "";
	std::vector<std::string> names = splitByComma(args[0]);

	for (size_t i = 0; i < names.size(); ++i)
	{
		Channel* channel = findChannel(names[i]);

		if (channel == NULL)
		{
			sendNumeric(client, "403", names[i] + " :No such channel");
			continue;
		}
		if (!channel->hasMember(client))
		{
			sendNumeric(client, "442", channel->getName() + " :You're not on that channel");
			continue;
		}

		std::string message = ":" + client->getPrefix() + " PART " + channel->getName();
		if (!reason.empty())
		{
			message += " :" + reason;
		}
		broadcastToChannel(channel, message, NULL);
		channel->removeMember(client);
		removeChannelIfEmpty(channel);  // 마지막 인원이면 채널 소멸
	}
}

/**
 * @brief TOPIC <channel> [:<topic>]
 *
 * - 인자 1개      : 조회 → 332(+333) 또는 331
 * - 인자 2개      : 변경 → +t 이면 운영자만 가능(482). "TOPIC #a :" 는 토픽 삭제
 */
void Server::handleTopic(Client* client, const std::vector<std::string>& args)
{
	if (args.empty() || args[0].empty())
	{
		sendNumeric(client, "461", "TOPIC :Not enough parameters");
		return;
	}

	Channel* channel = findChannel(args[0]);
	if (channel == NULL)
	{
		sendNumeric(client, "403", args[0] + " :No such channel");
		return;
	}
	if (!channel->hasMember(client))
	{
		sendNumeric(client, "442", channel->getName() + " :You're not on that channel");
		return;
	}
	if (args.size() == 1)
	{
		sendTopicReply(client, channel);
		return;
	}
	if (channel->isTopicRestricted() && !channel->isOperator(client))
	{
		sendNumeric(client, "482", channel->getName() + " :You're not channel operator");
		return;
	}

	std::string topic = args[1].substr(0, TOPIC_MAX_LENGTH);

	channel->setTopic(topic, client->getNickname());
	broadcastToChannel(channel, ":" + client->getPrefix() + " TOPIC " + channel->getName()
		+ " :" + topic, NULL);
}

/**
 * @brief INVITE <nickname> <channel>
 *
 * - 운영자만 사용 가능(482), 초대자는 채널 멤버여야 함(442)
 * - 대상이 이미 멤버면 443 ERR_USERONCHANNEL
 * - 성공: 초대 목록 등록 → 초대자에게 341 RPL_INVITING, 대상에게 INVITE 메시지
 */
void Server::handleInvite(Client* client, const std::vector<std::string>& args)
{
	if (args.size() < 2 || args[0].empty() || args[1].empty())
	{
		sendNumeric(client, "461", "INVITE :Not enough parameters");
		return;
	}

	Client* target = findClientByNick(args[0]);
	if (target == NULL || !target->isRegistered())
	{
		sendNumeric(client, "401", args[0] + " :No such nick/channel");
		return;
	}

	Channel* channel = findChannel(args[1]);
	if (channel == NULL)
	{
		sendNumeric(client, "403", args[1] + " :No such channel");
		return;
	}
	if (!channel->hasMember(client))
	{
		sendNumeric(client, "442", channel->getName() + " :You're not on that channel");
		return;
	}
	if (!channel->isOperator(client))
	{
		sendNumeric(client, "482", channel->getName() + " :You're not channel operator");
		return;
	}
	if (channel->hasMember(target))
	{
		sendNumeric(client, "443", target->getNickname() + " " + channel->getName()
			+ " :is already on channel");
		return;
	}

	channel->addInvite(target->getNickname());
	sendNumeric(client, "341", target->getNickname() + " " + channel->getName());
	sendToClient(target, ":" + client->getPrefix() + " INVITE " + target->getNickname()
		+ " :" + channel->getName());
}

/**
 * @brief KICK <channel> <user>{,<user>} [:<comment>]
 *
 * - 운영자만 가능(482), 대상이 채널에 없으면 441
 * - KICK 메시지를 대상 포함 전체에 브로드캐스트한 뒤 대상 제거
 */
void Server::handleKick(Client* client, const std::vector<std::string>& args)
{
	if (args.size() < 2 || args[0].empty() || args[1].empty())
	{
		sendNumeric(client, "461", "KICK :Not enough parameters");
		return;
	}

	Channel* channel = findChannel(args[0]);
	if (channel == NULL)
	{
		sendNumeric(client, "403", args[0] + " :No such channel");
		return;
	}
	if (!channel->hasMember(client))
	{
		sendNumeric(client, "442", channel->getName() + " :You're not on that channel");
		return;
	}
	if (!channel->isOperator(client))
	{
		sendNumeric(client, "482", channel->getName() + " :You're not channel operator");
		return;
	}

	std::string reason = (args.size() > 2 && !args[2].empty()) ? args[2] : client->getNickname();
	std::vector<std::string> nicks = splitByComma(args[1]);

	for (size_t i = 0; i < nicks.size(); ++i)
	{
		Client* target = findClientByNick(nicks[i]);

		if (target == NULL)
		{
			sendNumeric(client, "401", nicks[i] + " :No such nick/channel");
			continue;
		}
		if (!channel->hasMember(target))
		{
			sendNumeric(client, "441", target->getNickname() + " " + channel->getName()
				+ " :They aren't on that channel");
			continue;
		}
		broadcastToChannel(channel, ":" + client->getPrefix() + " KICK " + channel->getName()
			+ " " + target->getNickname() + " :" + reason, NULL);
		channel->removeMember(target);
	}
	removeChannelIfEmpty(channel);  // 운영자가 자기 자신을 KICK 해서 비는 경우 대비
}

/**
 * @brief MODE <target> [<modestring> [<mode arguments>...]]
 *
 * - 대상이 채널이 아니면 유저 모드: 지원 모드가 없으므로 조회(221)만 응답
 * - MODE #chan          : 324 RPL_CHANNELMODEIS + 329 RPL_CREATIONTIME
 * - MODE #chan b        : 밴 목록 조회(irssi 자동 전송) → 빈 목록 368
 * - MODE #chan +it-k .. : 운영자만 가능(482) → applyChannelModes()
 */
void Server::handleMode(Client* client, const std::vector<std::string>& args)
{
	if (args.empty() || args[0].empty())
	{
		sendNumeric(client, "461", "MODE :Not enough parameters");
		return;
	}

	const std::string& target = args[0];

	if (!isChannelName(target))
	{
		Client* user = findClientByNick(target);

		if (user == NULL)
		{
			sendNumeric(client, "401", target + " :No such nick/channel");
		}
		else if (user != client)
		{
			sendNumeric(client, "502", ":Cant change mode for other users");
		}
		else if (args.size() == 1)
		{
			sendNumeric(client, "221", "+");
		}
		return;
	}

	Channel* channel = findChannel(target);
	if (channel == NULL)
	{
		sendNumeric(client, "403", target + " :No such channel");
		return;
	}
	if (args.size() == 1 || args[1].empty())
	{
		sendNumeric(client, "324", channel->getName() + " "
			+ channel->getModeString(channel->hasMember(client)));
		sendNumeric(client, "329", channel->getName() + " "
			+ toString(static_cast<unsigned long>(channel->getCreationTime())));
		return;
	}
	if (args.size() == 2 && (args[1] == "b" || args[1] == "+b"))
	{
		sendNumeric(client, "368", channel->getName() + " :End of channel ban list");
		return;
	}
	if (!channel->isOperator(client))
	{
		sendNumeric(client, "482", channel->getName() + " :You're not channel operator");
		return;
	}
	applyChannelModes(client, channel, args);
}

/**
 * @brief 모드 문자열을 한 글자씩 순회하며 적용합니다.
 *
 * [파싱 규칙]  예) MODE #chan +kl-i secret 10
 *   args[1] = "+kl-i"  : 모드 문자열 (부호 상태를 유지하며 순회)
 *   args[2..]          : 인자가 필요한 모드가 "등장한 순서대로" 하나씩 소비 (argIndex 증가)
 *
 *   문자 | 부호 상태 | 동작                   | 인자 소비
 *   ----+----------+------------------------+-----------
 *   '+' |  + 로 전환 |                        |
 *   'k' |  +       | 키 = "secret"          | args[2]
 *   'l' |  +       | 제한 = 10              | args[3]
 *   '-' |  - 로 전환 |                        |
 *   'i' |  -       | 초대 전용 해제          | 없음
 *
 * [인자 소비 표]
 *   i, t     : 인자 없음
 *   k        : +k 필수 / -k 는 인자가 있으면 소비만 (RFC 호환)
 *   o        : +o, -o 모두 필수 (대상 닉네임)
 *   l        : +l 필수 / -l 없음
 *
 * 실제로 상태가 바뀐 모드만 모아 ":nick!u@h MODE #chan +kl-i secret 10" 형태로 한 번에 브로드캐스트합니다.
 * (이미 +i인데 또 +i를 요청한 경우 등은 브로드캐스트에서 제외)
 */
void Server::applyChannelModes(Client* client, Channel* channel, const std::vector<std::string>& args)
{
	const std::string& modeString = args[1];
	size_t             argIndex = 2;
	char               sign = '+';
	char               lastSign = '\0';
	std::string        appliedModes;
	std::string        appliedParams;

	for (size_t i = 0; i < modeString.size(); ++i)
	{
		char mode = modeString[i];

		if (mode == '+' || mode == '-')
		{
			sign = mode;
			continue;
		}

		bool adding = (sign == '+');

		switch (mode)
		{
			case 'i':
				if (channel->isInviteOnly() != adding)
				{
					channel->setInviteOnly(adding);
					appendModeChange(appliedModes, appliedParams, lastSign, sign, mode, "");
				}
				break;

			case 't':
				if (channel->isTopicRestricted() != adding)
				{
					channel->setTopicRestricted(adding);
					appendModeChange(appliedModes, appliedParams, lastSign, sign, mode, "");
				}
				break;

			case 'k':
				if (adding)
				{
					if (argIndex >= args.size())
					{
						sendNumeric(client, "461", "MODE :Not enough parameters");
						break;
					}
					std::string key = args[argIndex++];
					if (!isValidKey(key))
					{
						sendNumeric(client, "525", channel->getName() + " :Key is not well-formed");
						break;
					}
					channel->setKey(key);
					appendModeChange(appliedModes, appliedParams, lastSign, sign, mode, key);
				}
				else
				{
					if (argIndex < args.size())
					{
						++argIndex;
					}
					if (channel->hasKey())
					{
						channel->clearKey();
						appendModeChange(appliedModes, appliedParams, lastSign, sign, mode, "");
					}
				}
				break;

			case 'o':
			{
				if (argIndex >= args.size())
				{
					sendNumeric(client, "461", "MODE :Not enough parameters");
					break;
				}
				std::string nick = args[argIndex++];
				Client*     target = findClientByNick(nick);
				if (target == NULL)
				{
					sendNumeric(client, "401", nick + " :No such nick/channel");
					break;
				}
				if (!channel->hasMember(target))
				{
					sendNumeric(client, "441", target->getNickname() + " " + channel->getName()
						+ " :They aren't on that channel");
					break;
				}
				if (adding && !channel->isOperator(target))
				{
					channel->addOperator(target);
					appendModeChange(appliedModes, appliedParams, lastSign, sign, mode, target->getNickname());
				}
				else if (!adding && channel->isOperator(target))
				{
					channel->removeOperator(target);
					appendModeChange(appliedModes, appliedParams, lastSign, sign, mode, target->getNickname());
				}
				break;
			}

			case 'l':
				if (adding)
				{
					if (argIndex >= args.size())
					{
						sendNumeric(client, "461", "MODE :Not enough parameters");
						break;
					}
					std::string limitStr = args[argIndex++];
					size_t      limit = 0;
					if (!parseLimit(limitStr, limit))
					{
						sendNumeric(client, "696", channel->getName() + " l " + limitStr
							+ " :Invalid limit");
						break;
					}
					if (channel->getLimit() != limit)
					{
						channel->setLimit(limit);
						appendModeChange(appliedModes, appliedParams, lastSign, sign, mode, toString(limit));
					}
				}
				else if (channel->hasLimit())
				{
					channel->setLimit(0);
					appendModeChange(appliedModes, appliedParams, lastSign, sign, mode, "");
				}
				break;

			default:
				sendNumeric(client, "472", std::string(1, mode) + " :is unknown mode char to me for "
					+ channel->getName());
				break;
		}
	}

	if (!appliedModes.empty())
	{
		broadcastToChannel(channel, ":" + client->getPrefix() + " MODE " + channel->getName()
			+ " " + appliedModes + appliedParams, NULL);
	}
}
