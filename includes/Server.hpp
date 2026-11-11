#ifndef SERVER_HPP
#define SERVER_HPP

#include <map>
#include <string>
#include <vector>
#include <poll.h>

// Forward declarations (헤더 순환 참조 방지 및 컴파일 경량화)
class Client;
class Channel;

/**
 * @brief ft_irc 서버 본체: 단일 poll() 이벤트 루프 + 모든 Mandatory 커맨드 핸들러
 *
 * [전체 데이터 흐름 한눈에 보기]
 *
 *   poll() ──POLLIN(listen fd)──> handleNewConnection() ─ accept + O_NONBLOCK + pollfd 등록
 *      │
 *      ├──POLLIN(client fd)──> handleClientRead()
 *      │        recv() → _recvBuffer 누적 → getLineFromBuffer()로 "\r\n" 단위 추출
 *      │        → processLine() → commandParser() → _commandHandlers[name] 호출
 *      │        → 핸들러는 sendToClient()로 "큐잉만" 함 (+ POLLOUT ON)
 *      │
 *      ├──POLLOUT(client fd)─> handleClientWrite()
 *      │        send() → 보낸 만큼 _sendBuffer에서 제거 → 비면 POLLOUT OFF
 *      │
 *      └──POLLERR/POLLHUP/POLLNVAL─> quitClient() → (루프 끝) flushPendingDisconnects()
 */
class Server
{
private:
	// 커맨드 이름 → 멤버 함수 포인터 (if-else 사다리 대신 테이블 디스패치)
	typedef void (Server::*CommandHandler)(Client*, const std::vector<std::string>&);

	int                                     _port;
	std::string                             _password;
	int                                     _serverFd;
	bool                                    _isRunning;

	std::string                             _serverName;
	std::string                             _version;
	std::string                             _creationDate;

	std::vector<struct pollfd>              _pollFds;          // [0]은 항상 listen 소켓
	std::map<int, Client*>                  _clients;          // fd → Client*
	std::map<std::string, Channel*>         _channels;         // ircToLower(채널명) → Channel*
	std::map<std::string, CommandHandler>   _commandHandlers;  // "JOIN" → &Server::handleJoin

	// 소켓 및 포인터 자원 공유 방지를 위한 복사 생성자 / 대입 연산자 비공개화 (Non-copyable)
	Server(const Server& other);
	Server& operator=(const Server& other);

	// ---------- 초기화 / 종료 ----------
	void        setupSignals();
	void        initCommandHandlers();
	void        initSocket();
	void        cleanup();

	// ---------- poll() 이벤트 루프 ----------
	void        processPollEvents();
	void        handleNewConnection();
	void        handleClientRead(Client* client);
	void        handleClientWrite(Client* client);
	void        disconnectClient(int fd);
	void        flushPendingDisconnects();

	// ---------- pollfd 이벤트 토글 ----------
	struct pollfd*  findPollFd(int fd);
	void        enablePollOut(int fd);
	void        disablePollOut(int fd);
	void        disablePollIn(int fd);

	// ---------- 송신 (큐잉 전용: 실제 send()는 handleClientWrite에서만) ----------
	void        sendToClient(Client* client, const std::string& message);
	void        sendNumeric(Client* client, const std::string& code, const std::string& params);
	void        broadcastToChannel(Channel* channel, const std::string& message, Client* exclude);
	void        broadcastToPeers(Client* client, const std::string& message, bool includeSelf);

	// ---------- 명령 처리 공통 ----------
	void        processLine(Client* client, const std::string& line);
	void        tryCompleteRegistration(Client* client);
	void        quitClient(Client* client, const std::string& reason, bool graceful);
	void        announceQuit(Client* client, const std::string& reason);

	// ---------- 조회 / 채널 유틸 ----------
	Client*     findClientByNick(const std::string& nickname) const;
	Channel*    findChannel(const std::string& name) const;
	void        removeChannelIfEmpty(Channel* channel);
	void        joinSingleChannel(Client* client, const std::string& name, const std::string& key);
	void        partAllChannels(Client* client);
	void        sendTopicReply(Client* client, Channel* channel);
	void        sendNamesReply(Client* client, Channel* channel);
	void        sendWhoReply(Client* client, const std::string& channelName, Client* target, bool isOp);
	void        relayMessage(Client* client, const std::vector<std::string>& args,
							 const std::string& command, bool sendErrors);
	void        applyChannelModes(Client* client, Channel* channel, const std::vector<std::string>& args);

	// ---------- 커맨드 핸들러: 인증 / 기본 통신 (sisung) ----------
	void        handleCap(Client* client, const std::vector<std::string>& args);
	void        handlePass(Client* client, const std::vector<std::string>& args);
	void        handleNick(Client* client, const std::vector<std::string>& args);
	void        handleUser(Client* client, const std::vector<std::string>& args);
	void        handlePing(Client* client, const std::vector<std::string>& args);
	void        handlePong(Client* client, const std::vector<std::string>& args);
	void        handleQuit(Client* client, const std::vector<std::string>& args);
	void        handlePrivmsg(Client* client, const std::vector<std::string>& args);
	void        handleNotice(Client* client, const std::vector<std::string>& args);
	void        handleWho(Client* client, const std::vector<std::string>& args);

	// ---------- 커맨드 핸들러: 채널 / 운영자 (jejeon) ----------
	void        handleJoin(Client* client, const std::vector<std::string>& args);
	void        handlePart(Client* client, const std::vector<std::string>& args);
	void        handleTopic(Client* client, const std::vector<std::string>& args);
	void        handleInvite(Client* client, const std::vector<std::string>& args);
	void        handleKick(Client* client, const std::vector<std::string>& args);
	void        handleMode(Client* client, const std::vector<std::string>& args);

public:
	Server(int port, const std::string& password);
	~Server();

	void run();
	void stop();
};

#endif
