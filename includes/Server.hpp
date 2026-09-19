#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <vector>
#include <map>
#include <poll.h>

// Forward declarations (헤더 순환 참조 방지 및 컴파일 경량화)
class Client;
class Channel;

class Server 
{
private:
    int                             _port;
    std::string                     _password;
    int                             _serverFd;
    bool                            _isRunning;

    std::vector<struct pollfd>      _pollFds;
    std::map<int, Client*>          _clients;
    std::map<std::string, Channel*> _channels;

    // 소켓 및 포인터 자원 공유 방지를 위한 복사 생성자 / 대입 연산자 비공개화 (Non-copyable)
    Server(const Server& other);
    Server& operator=(const Server& other);

    // 내부 초기화 및 소멸 루틴 (lowerCamelCase & Allman Style 적용)
    void initSocket();
    void cleanup();

    // 추후 구현될 I/O 핸들러 스텁
    void handleNewConnection();
    void handleClientRead(int fd);
    void handleClientWrite(int fd);
    void disconnectClient(int fd);

public:
    Server(int port, const std::string& password);
    ~Server();

    void run();
    void stop();
};

#endif
