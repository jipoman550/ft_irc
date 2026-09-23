#ifndef PARSER_HPP
#define PARSER_HPP

#include <string>
#include "Command.hpp"

// IRC 명령어 한 줄을 Command 구조체로 변환 합니다.
Command commandParser(const std::string& line);

// IRC 메세지 끝의 '\r\n' 개행 문자를 제거 합니다.
void removeLineEnding(std::string& input);

// 명령어 앞의 선행 공백을 제거하고 prefix가 존재하면 건너뛴니다.
void preprocessInput(std::string& input);

// 명령어 이름을 대문자로 변환해 대소문자를 통일 합니다.
void normalizeCommand(std::string& input);

#endif