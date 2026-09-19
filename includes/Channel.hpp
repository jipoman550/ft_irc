#pragma once

#include <string>
#include <vector>

struct client; // 전방선언

struct Channel
{
    std::string name;
    std::vector<client*> members;
    std::vector<client*> operators;
    std::string topic;
    std::string key;
    bool inviteOnly;
    bool topicRestricted;
    int limit;
};