#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <cstddef>
#include <string>

/**
 * @brief 서버에 접속한 IRC 클라이언트 1명의 상태를 보관하는 클래스
 *
 * - 소켓 fd, 닉네임/유저명/호스트 등 신원 정보
 * - 인증 단계 상태 (PASS 통과 여부, 등록 완료 여부)
 * - 수신 버퍼(_recvBuffer): recv()로 받은 조각난 데이터를 "\r\n"이 올 때까지 누적
 * - 송신 버퍼(_sendBuffer): 보낼 메시지를 쌓아두고, poll()이 POLLOUT을 알려줄 때만 send()
 *
 * [Defense 포인트] "왜 send()를 바로 호출하지 않나요?"
 *  -> 논블로킹 소켓에 바로 send()하면 커널 송신 버퍼가 가득 찼을 때 일부만 전송되거나
 *     EAGAIN이 발생합니다. 큐잉 후 POLLOUT(쓰기 가능) 이벤트에서만 send()해야
 *     "poll() 없이 I/O 금지" 규칙을 지키면서 데이터 유실도 막을 수 있습니다.
 */
class Client
{
private:
	int         _fd;
	std::string _hostname;
	std::string _nickname;
	std::string _username;
	std::string _realname;

	bool        _passAccepted;   // PASS 명령 통과 여부
	int         _passAttempts;   // 잘못된 PASS 시도 횟수 (무차별 대입 방지)
	bool        _registered;     // PASS + NICK + USER 모두 완료 여부
	bool        _closing;        // 종료 예약 상태 (송신 버퍼를 비운 뒤 close)
	bool        _quitAnnounced;  // 채널 동료들에게 QUIT 브로드캐스트를 이미 했는지

	std::string _recvBuffer;     // 부분 패킷(Partial data) 누적 버퍼
	std::string _sendBuffer;     // 송신 대기열 (POLLOUT 시 소진)

	// fd와 버퍼를 가진 객체이므로 복사 금지 (Non-copyable)
	Client();
	Client(const Client& other);
	Client& operator=(const Client& other);

public:
	Client(int fd, const std::string& hostname);
	~Client();

	// ---------- Getter ----------
	int                 getFd() const;
	const std::string&  getHostname() const;
	const std::string&  getNickname() const;
	const std::string&  getUsername() const;
	const std::string&  getRealname() const;
	std::string         getPrefix() const;

	bool                isPassAccepted() const;
	int                 getPassAttempts() const;
	bool                isRegistered() const;
	bool                isClosing() const;
	bool                isQuitAnnounced() const;

	// ---------- Setter ----------
	void                setNickname(const std::string& nickname);
	void                setUsername(const std::string& username);
	void                setRealname(const std::string& realname);
	void                setPassAccepted(bool accepted);
	void                increasePassAttempts();
	void                setRegistered(bool registered);
	void                setClosing(bool closing);
	void                setQuitAnnounced(bool announced);

	// ---------- 수신 버퍼 ----------
	std::string&        getRecvBuffer();
	void                appendToRecvBuffer(const char* data, size_t length);

	// ---------- 송신 버퍼 ----------
	const std::string&  getSendBuffer() const;
	void                queueMessage(const std::string& message);
	void                consumeSendBuffer(size_t length);
	void                clearSendBuffer();
	bool                hasPendingSend() const;
};

#endif