#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <vector>

class Client;

class Channel 
{
public:
    std::string          name;
    std::vector<Client*> members;
    std::vector<Client*> operators;
    std::string          topic;
    std::string          key;
    bool                 inviteOnly;
    bool                 topicRestricted;
    int                  limit;
};

#endif