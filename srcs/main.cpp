#include "Server.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <exception>

/**
 * @brief 입력받은 포트 문자열의 유효성을 검증하고 정수형으로 변환합니다. (lowerCamelCase 적용)
 * 
 * - 빈 문자열 검사
 * - 모든 문자가 숫자인지 검사 (음수/문자열 거부)
 * - std::istringstream을 이용해 오버플로우 및 변환 성공 여부 검사
 * - 유효 포트 범위 (1024 ~ 65535) 검사
 * 
 * @param str 포트 번호 문자열
 * @param port 변환된 포트 번호 저장 변수 (참조)
 * @return true 포트 번호가 유효한 경우
 * @return false 포트 번호가 유효하지 않은 경우
 */
static bool parsePort(const std::string& str, int& port) 
{
    // 1. 빈 문자열 예외 처리
    if (str.empty())
    { 
        return (false);
    }

    // 2. 문자열이 전적으로 0~9 숫자로만 구성되어 있는지 검사 (음수 기호 및 문자 거부)
    for (size_t i = 0; i < str.length(); ++i) 
    {
        if (str[i] < '0' || str[i] > '9') 
        {
            return (false);
        }
    }

    // 3. 문자열 스트림을 사용해 정수로 안전하게 파싱 및 오버플로우 감지
    std::istringstream iss(str);
    iss >> port;

    // 파싱 실패(fail)했거나 남은 문자가 있는 경우(eof가 아님) 실패 처리
    if (iss.fail() || !iss.eof()) 
    {
        return (false);
    }

    // 4. 일반 사용자가 사용 가능한 유효 포트 범위 (1024 ~ 65535) 검증
    return (port >= 1024 && port <= 65535);
}

/**
 * @brief 프로그램 진입점 (main 함수)
 * 
 * 명령줄 인자로 포트 번호와 비밀번호를 받아 유효성을 검증한 뒤 서버를 생성 및 실행합니다.
 * Usage: ./ircserv <port> <password>
 */
int main(int argc, char** argv) 
{
    // 1. 명령줄 인자 개수 검증 (실행파일명 + port + password = 3개)
    if (argc != 3) 
    {
        std::cerr << "Usage: ./ircserv <port> <password>" << std::endl;
        return (1);
    }

    // 2. 포트 번호 인자 유효성 검사 및 파싱
    int port = 0;
    if (!parsePort(argv[1], port)) 
    {
        std::cerr << "Error: Invalid port (1024 ~ 65535)" << std::endl;
        return (1);
    }

    // 3. 비밀번호 인자 유효성 검사 (빈 문자열 거부)
    std::string password = argv[2];
    if (password.empty()) 
    {
        std::cerr << "Error: Password cannot be empty" << std::endl;
        return (1);
    }

    // 4. Server 인스턴스 생성 및 실행 (예외 처리)
    try 
    {
        Server server(port, password);
        server.run();
    }
    catch (const std::exception& e) 
    {
        std::cerr << e.what() << std::endl;
        return (1);
    }

    return (0);
}
