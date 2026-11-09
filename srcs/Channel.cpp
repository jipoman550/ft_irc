#include "Channel.hpp"
#include "Client.hpp"

#include <algorithm>
#include <sstream>

/**
 * @brief RFC 1459 casemapping 기반 소문자 정규화
 */
std::string ircToLower(const std::string& str)
{
	std::string result = str;

	for (size_t i = 0; i < result.size(); ++i)
	{
		char c = result[i];

		if (c >= 'A' && c <= 'Z')
		{
			result[i] = static_cast<char>(c - 'A' + 'a');
		}
		else if (c == '[')
		{
			result[i] = '{';
		}
		else if (c == ']')
		{
			result[i] = '}';
		}
		else if (c == '\\')
		{
			result[i] = '|';
		}
		else if (c == '~')
		{
			result[i] = '^';
		}
	}
	return (result);
}

Channel::Channel(const std::string& name)
	: _name(name),
	  _topic(""),
	  _topicSetter(""),
	  _topicTime(0),
	  _creationTime(std::time(NULL)),
	  _key(""),
	  _limit(0),
	  _inviteOnly(false),
	  _topicRestricted(false)
{
}

/**
 * @brief Channel은 Client*를 소유하지 않으므로 delete하지 않습니다.
 */
Channel::~Channel()
{
}

// =============================== 기본 정보 ===============================

const std::string& Channel::getName() const
{
	return (_name);
}

std::time_t Channel::getCreationTime() const
{
	return (_creationTime);
}

// =============================== 토픽 ===============================

const std::string& Channel::getTopic() const
{
	return (_topic);
}

const std::string& Channel::getTopicSetter() const
{
	return (_topicSetter);
}

std::time_t Channel::getTopicTime() const
{
	return (_topicTime);
}

void Channel::setTopic(const std::string& topic, const std::string& setter)
{
	_topic = topic;
	_topicSetter = setter;
	_topicTime = std::time(NULL);
}

// =============================== 모드 ===============================

bool Channel::isInviteOnly() const
{
	return (_inviteOnly);
}

void Channel::setInviteOnly(bool enabled)
{
	_inviteOnly = enabled;
}

bool Channel::isTopicRestricted() const
{
	return (_topicRestricted);
}

void Channel::setTopicRestricted(bool enabled)
{
	_topicRestricted = enabled;
}

bool Channel::hasKey() const
{
	return (!_key.empty());
}

const std::string& Channel::getKey() const
{
	return (_key);
}

void Channel::setKey(const std::string& key)
{
	_key = key;
}

void Channel::clearKey()
{
	_key.clear();
}

bool Channel::hasLimit() const
{
	return (_limit > 0);
}

size_t Channel::getLimit() const
{
	return (_limit);
}

void Channel::setLimit(size_t limit)
{
	_limit = limit;
}

/**
 * @brief +l 인원 제한에 도달했는지 검사합니다. (제한 0 = 무제한)
 */
bool Channel::isFull() const
{
	return (_limit > 0 && _members.size() >= _limit);
}

/**
 * @brief 324 RPL_CHANNELMODEIS 용 모드 문자열을 생성합니다.
 *
 * 예) "+itkl secret 10"
 * @param includeParams true면 키/제한 값까지 포함 (채널 멤버에게만 키를 노출)
 */
std::string Channel::getModeString(bool includeParams) const
{
	std::string modes = "+";
	std::string params;

	if (_inviteOnly)
	{
		modes += "i";
	}
	if (_topicRestricted)
	{
		modes += "t";
	}
	if (hasKey())
	{
		modes += "k";
		if (includeParams)
		{
			params += " " + _key;
		}
	}
	if (hasLimit())
	{
		std::ostringstream oss;

		oss << _limit;
		modes += "l";
		if (includeParams)
		{
			params += " " + oss.str();
		}
	}
	return (modes + params);
}

// =============================== 멤버 ===============================

const std::vector<Client*>& Channel::getMembers() const
{
	return (_members);
}

size_t Channel::getMemberCount() const
{
	return (_members.size());
}

bool Channel::isEmpty() const
{
	return (_members.empty());
}

bool Channel::hasMember(const Client* client) const
{
	return (std::find(_members.begin(), _members.end(), client) != _members.end());
}

void Channel::addMember(Client* client)
{
	if (!hasMember(client))
	{
		_members.push_back(client);
	}
}

/**
 * @brief 멤버를 제거합니다. 운영자 목록에서도 함께 제거해 불변식(ops ⊆ members)을 유지합니다.
 */
void Channel::removeMember(Client* client)
{
	std::vector<Client*>::iterator it = std::find(_members.begin(), _members.end(), client);

	if (it != _members.end())
	{
		_members.erase(it);
	}
	removeOperator(client);
}

/**
 * @brief 353 RPL_NAMREPLY 용 멤버 목록 문자열 ("@alice bob carol")
 */
std::string Channel::getNamesList() const
{
	std::string names;

	for (size_t i = 0; i < _members.size(); ++i)
	{
		if (!names.empty())
		{
			names += " ";
		}
		if (isOperator(_members[i]))
		{
			names += "@";
		}
		names += _members[i]->getNickname();
	}
	return (names);
}

// =============================== 운영자 ===============================

bool Channel::isOperator(const Client* client) const
{
	return (std::find(_operators.begin(), _operators.end(), client) != _operators.end());
}

void Channel::addOperator(Client* client)
{
	if (hasMember(client) && !isOperator(client))
	{
		_operators.push_back(client);
	}
}

void Channel::removeOperator(Client* client)
{
	std::vector<Client*>::iterator it = std::find(_operators.begin(), _operators.end(), client);

	if (it != _operators.end())
	{
		_operators.erase(it);
	}
}

// =============================== 초대 목록 ===============================

bool Channel::isInvited(const std::string& nickname) const
{
	return (_invited.find(ircToLower(nickname)) != _invited.end());
}

void Channel::addInvite(const std::string& nickname)
{
	_invited.insert(ircToLower(nickname));
}

void Channel::removeInvite(const std::string& nickname)
{
	_invited.erase(ircToLower(nickname));
}

/**
 * @brief 초대받은 사용자가 NICK을 바꿔도 초대가 유지되도록 키를 갱신합니다.
 */
void Channel::renameInvite(const std::string& oldNick, const std::string& newNick)
{
	if (isInvited(oldNick))
	{
		removeInvite(oldNick);
		addInvite(newNick);
	}
}
