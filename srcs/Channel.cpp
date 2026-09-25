#include "Channel.hpp"

Channel::Channel() : inviteOnly(false), topicRestricted(false), limit(0)
{

}

Channel::~Channel()
{

}

void Channel::setName(const std::string& newName)
{
	name = newName;
}

const std::string& Channel::getName() const
{
	return (name);
}

bool Channel::isMember(Client* client) const
{
	for (size_t i = 0; i < members.size(); i++)
	{
		if (client == members[i])
		{
			return (true);
		}
	}
	return (false);
}

bool Channel::isOperator(Client* client) const
{
	for (size_t i = 0; i < operators.size(); i++)
	{
		if (client == operators[i])
		{
			return (true);
		}
	}
	return (false);
}

void Channel::addMember(Client* client)
{
	if (!isMember(client))
	{
		members.push_back(client);
	}
}

void Channel::removeMember(Client* client)
{
	for (size_t i = 0; i < members.size(); i++)
	{
		if (members[i] == client)
		{
			members.erase(members.begin() + i);

			for (size_t j = 0; j < operators.size(); j++)
			{
				if (operators[j] == client)
				{
					operators.erase(operators.begin() + j);
					break;
				}
			}
			return;
		}
	}
}

void Channel::addOperator(Client* client)
{
	if (!isOperator(client) && isMember(client))
	{
		operators.push_back(client);
	}
}

void Channel::removeOperator(Client* client)
{
	for (size_t j = 0; j < operators.size(); j++)
	{
		if (operators[j] == client)
		{
			operators.erase(operators.begin() + j);
			return;
		}
	}
}

void Channel::setTopic(const std::string& newTopic)
{
	topic = newTopic;
}

const std::string& Channel::getTopic() const
{
	return (topic);
}

void Channel::setKey(const std::string& newkey)
{
	key = newkey;
}

const std::string& Channel::getKey() const
{
	return (key);
}

void Channel::clearKey()
{
	key.clear();
}

void Channel::setInviteOnly(bool value)
{
	inviteOnly = value;
}

bool Channel::isInviteOnly() const
{
	return (inviteOnly);
}

void Channel::setTopicRestricted(bool value)
{
	topicRestricted = value;
}

bool Channel::isTopicRestricted() const
{
	return (topicRestricted);
}

void Channel::setLimit(int newLimit)
{
	limit = newLimit;
}

int Channel::getLimit() const
{
	return (limit);
}

void Channel::clearLimit()
{
	limit = 0;
}