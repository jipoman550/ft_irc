#include "Server.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdexcept>

Server::Server(int port, const std::string& password)
    : _port(port), _password(password), _serverFd(-1), _isRunning(false) 
{
}

Server::~Server() 
{
    cleanup();
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
    int opt = 1;
    if (setsockopt(_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) 
    {
        close(_serverFd);
        _serverFd = -1;
        throw std::runtime_error("Error: setsockopt(SO_REUSEADDR) failed");
    }

    // 3. 논블로킹 모드 설정 (서브젝트 필수 요구사항)
    if (fcntl(_serverFd, F_SETFL, O_NONBLOCK) < 0) 
    {
        close(_serverFd);
        _serverFd = -1;
        throw std::runtime_error("Error: Failed to set non-blocking on server socket");
    }

    // 4. 주소 바인딩
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
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

    // 6. 서버 소켓을 poll 목록의 첫 번째 원소로 등록 (POLLIN 감시)
    struct pollfd pfd;
    pfd.fd = _serverFd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    _pollFds.push_back(pfd);

    std::cout << "[INFO] Server listening on port " << _port << std::endl;
}

void Server::run() 
{
    initSocket();
    _isRunning = true;

    // TODO (sisung): poll() 이벤트 루프 구현 시작 지점
    // while (_isRunning) {
    //     int ret = poll(&_pollFds[0], _pollFds.size(), -1);
    //     ...
    // }
}

void Server::stop() 
{
    _isRunning = false;
}

void Server::cleanup() 
{
    // 1. 모든 열린 클라이언트 및 서버 소켓 닫기
    for (size_t i = 0; i < _pollFds.size(); ++i) 
    {
        if (_pollFds[i].fd >= 0) 
        {
            close(_pollFds[i].fd);
        }
    }
    _pollFds.clear();

    // 2. 동적 할당된 클라이언트 및 채널 인스턴스 메모리 해제 (Valgrind 누수 방지)
    // for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
    //     delete it->second;
    // }
    _clients.clear();

    // for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
    //     delete it->second;
    // }
    _channels.clear();

    _serverFd = -1;
    std::cout << "[INFO] Server cleanup finished." << std::endl;
}

void Server::handleNewConnection() 
{
}

void Server::handleClientRead(int fd) 
{
    (void)fd;
}

void Server::handleClientWrite(int fd) 
{
    (void)fd;
}

void Server::disconnectClient(int fd) 
{
    (void)fd;
}
