#include "Parser.hpp"

/*
	IRC 명령어 한 줄을 파싱 합니다.
	첫 번째 토큰은 명령어 이름으로 저장하고,
	나머지 토큰은 파라미터로 저장합니다.
	':' 이후의 문자열은 하나의 trailing parameter로 처리합니다.
*/ 
Command commandParser(const std::string& line)
{
	Command result;
	size_t pos;
	size_t start;

	pos = line.find(' ');
	result.name = line.substr(0, pos);
	if (pos == std::string::npos)
	{
		return (result);
	}

	while (1)
	{
		start = pos + 1;

		while (start < line.size() && line[start] == ' ')
		{
			start++;
			if (start == line.size())
			{
				return (result);
			}
		}

		if (line[start] == ':')
		{
			result.params.push_back(line.substr(start + 1));
			return (result);
		}

		pos = line.find(' ', start);

		if (pos == std::string::npos)
		{
			result.params.push_back(line.substr(start));
			break;
		}

		result.params.push_back(line.substr(start, pos - start));
	}
	return (result);
}