#include "Client.hpp"

Client::Client(int fd, const std::string& hostname)
	: _fd(fd),
	  _hostname(hostname),
	  _nickname(""),
	  _username(""),
	  _realname(""),
	  _passAccepted(false),
	  _passAttempts(0),
	  _registered(false),
	  _closing(false),
	  _quitAnnounced(false),
	  _recvBuffer(""),
	  _sendBuffer("")
{
}

/**
 * @brief 소멸자: fd close는 Server::disconnectClient()의 책임입니다.
 *
 * Client가 직접 close하지 않는 이유: poll 목록(_pollFds)에서의 제거와 close 시점을
 * Server 한 곳에서 관리해야 "닫힌 fd를 poll에 넘기는" 실수(POLLNVAL)를 막을 수 있습니다.
 */
Client::~Client()
{
}

// =============================== Getter ===============================

int Client::getFd() const
{
	return (_fd);
}

const std::string& Client::getHostname() const
{
	return (_hostname);
}

const std::string& Client::getNickname() const
{
	return (_nickname);
}

const std::string& Client::getUsername() const
{
	return (_username);
}

const std::string& Client::getRealname() const
{
	return (_realname);
}

/**
 * @brief IRC 메시지 출처 prefix "nick!user@host"를 반환합니다.
 *
 * 다른 사용자에게 전달되는 모든 메시지(PRIVMSG, JOIN, MODE...)는
 * ":alice!alice@127.0.0.1 PRIVMSG #chan :hi" 처럼 이 prefix로 시작합니다.
 */
std::string Client::getPrefix() const
{
	std::string nick = _nickname.empty() ? "*" : _nickname;
	std::string user = _username.empty() ? "*" : _username;

	return (nick + "!" + user + "@" + _hostname);
}

bool Client::isPassAccepted() const
{
	return (_passAccepted);
}

int Client::getPassAttempts() const
{
	return (_passAttempts);
}

bool Client::isRegistered() const
{
	return (_registered);
}

bool Client::isClosing() const
{
	return (_closing);
}

bool Client::isQuitAnnounced() const
{
	return (_quitAnnounced);
}

// =============================== Setter ===============================

void Client::setNickname(const std::string& nickname)
{
	_nickname = nickname;
}

void Client::setUsername(const std::string& username)
{
	_username = username;
}

void Client::setRealname(const std::string& realname)
{
	_realname = realname;
}

void Client::setPassAccepted(bool accepted)
{
	_passAccepted = accepted;
}

void Client::increasePassAttempts()
{
	++_passAttempts;
}

void Client::setRegistered(bool registered)
{
	_registered = registered;
}

void Client::setClosing(bool closing)
{
	_closing = closing;
}

void Client::setQuitAnnounced(bool announced)
{
	_quitAnnounced = announced;
}

// =============================== 수신 버퍼 ===============================

/**
 * @brief 수신 버퍼 참조를 반환합니다. (getLineFromBuffer가 직접 앞부분을 잘라냄)
 */
std::string& Client::getRecvBuffer()
{
	return (_recvBuffer);
}

/**
 * @brief recv()로 받은 원시 바이트를 수신 버퍼 뒤에 이어 붙입니다.
 *
 * [Partial packet 파이프라인 1단계]
 * TCP는 "메시지 경계"가 없는 바이트 스트림입니다. "NICK al" + "ice\r\n" 처럼
 * 쪼개져 도착할 수 있으므로 일단 누적하고, "\r\n"이 완성될 때만 한 줄로 꺼냅니다.
 */
void Client::appendToRecvBuffer(const char* data, size_t length)
{
	_recvBuffer.append(data, length);
}

// =============================== 송신 버퍼 ===============================

const std::string& Client::getSendBuffer() const
{
	return (_sendBuffer);
}

/**
 * @brief 보낼 메시지를 송신 대기열 끝에 추가합니다. (실제 전송은 POLLOUT 시점)
 */
void Client::queueMessage(const std::string& message)
{
	_sendBuffer += message;
}

/**
 * @brief send()가 실제로 보낸 바이트 수만큼 대기열 앞부분을 제거합니다.
 *
 * send()는 요청한 길이보다 적게 보낼 수 있습니다(partial write).
 * 남은 부분은 다음 POLLOUT 이벤트에서 이어서 전송됩니다.
 */
void Client::consumeSendBuffer(size_t length)
{
	if (length >= _sendBuffer.size())
	{
		_sendBuffer.clear();
		return;
	}
	_sendBuffer.erase(0, length);
}

void Client::clearSendBuffer()
{
	_sendBuffer.clear();
}

bool Client::hasPendingSend() const
{
	return (!_sendBuffer.empty());
}
