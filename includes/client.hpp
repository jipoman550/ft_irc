#pragma once

#include <string>

struct Client
{
	int fd;
	std::string nickname;
	std::string username;
	bool auth;
	bool registered;
	std::string buffer;
};