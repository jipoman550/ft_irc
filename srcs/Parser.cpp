#include "Parser.hpp"

#include <cctype>

void removeLineEnding(std::string& input)
{
	if (input.empty())
	{
		return;
	}

	if (input[input.size() - 1] == '\n')
	{
		input.erase(input.size() - 1);
	} 

	if (!input.empty() && input[input.size() - 1] == '\r')
	{
		input.erase(input.size() - 1);
	}
}

void preprocessInput(std::string& input)
{
	size_t pos = 0;

	while (pos < input.size() && input[pos] == ' ')
	{
		pos++;
	}

	if (pos < input.size() && input[pos] == ':')
	{
		while (pos < input.size() && input[pos] != ' ')
		{
			pos++;
		}

		while (pos < input.size() && input[pos] == ' ')
		{
			pos++;
		}
	}

	input = input.substr(pos);
}

void normalizeCommand(std::string& input)
{
	for (size_t i = 0; i < input.size(); i++)
	{
		if (input[i] >= 'a' && input[i] <= 'z')
		{
			input[i] = std::toupper(input[i]);
		}
	}
}

/*
	IRC 명령어 한 줄을 파싱 합니다.
	첫 번째 토큰은 명령어 이름으로 저장하고,
	나머지 토큰은 파라미터로 저장합니다.
	':' 이후의 문자열은 하나의 trailing parameter로 처리합니다.
*/ 
Command commandParser(const std::string& line)
{
	Command result;
	std::string input = line;

	removeLineEnding(input);
	preprocessInput(input);

	size_t pos = 0;

	pos = input.find(' ');
	result.name = input.substr(0, pos);

	normalizeCommand(result.name);

	if (pos == std::string::npos)
	{
		return (result);
	}
	
	size_t start = pos + 1;

	while (start < input.size())
	{
		while (start < input.size() && input[start] == ' ')
		{
			start++;
		}

		if (start >= input.size())
		{
			break;
		}

		if (input[start] == ':')
		{
			result.trailing = input.substr(start + 1);
			break;
		}

		pos = input.find(' ', start);

		if (pos == std::string::npos)
		{
			result.params.push_back(input.substr(start));
			break;
		}

		result.params.push_back(input.substr(start, pos - start));
		start = pos + 1;
	}
	return (result);
}