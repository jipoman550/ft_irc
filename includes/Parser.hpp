#pragma once

#include <string>
#include "Command.hpp"

// IRC 명령어 한 줄을 Command 구조체로 변환 합니다.
Command commandParser(const std::string& line);