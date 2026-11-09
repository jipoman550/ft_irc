#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <cstddef>
#include <ctime>
#include <set>
#include <string>
#include <vector>

class Client;

/**
 * @brief IRC 대소문자 무시 비교용 정규화 함수 (닉네임/채널명 공용)
 *
 * RFC 1459 casemapping: 'A'~'Z' -> 'a'~'z', '[' -> '{', ']' -> '}', '\\' -> '|', '~' -> '^'
 * 예) "#Test" 와 "#test" 는 같은 채널, "Alice" 와 "alice" 는 같은 닉네임
 */
std::string ircToLower(const std::string& str);

/**
 * @brief IRC 채널 1개의 상태(멤버, 운영자, 초대 목록, 모드)를 보관하는 클래스
 *
 * Channel은 "데이터 + 규칙 검사"만 담당하고, 실제 메시지 송신(sendToClient)은 Server가 담당합니다.
 * (송신은 반드시 poll()의 POLLOUT 토글과 묶여야 하므로 Server 밖으로 새면 안 됩니다)
 *
 * 지원 모드:
 *  - i (_inviteOnly)      : 초대받은 사람만 JOIN 가능
 *  - t (_topicRestricted) : 운영자만 TOPIC 변경 가능
 *  - k (_key)             : JOIN 시 비밀번호 필요
 *  - o (_operators)       : 채널 운영자 권한
 *  - l (_limit)           : 최대 인원 제한 (0 = 제한 없음)
 *
 * [Defense 포인트] Channel은 Client*를 "빌려서" 들고 있을 뿐 소유하지 않습니다.
 *  -> Client 메모리 해제는 Server::disconnectClient()만 담당하며,
 *     해제 전에 반드시 모든 채널에서 removeMember()로 포인터를 제거합니다 (Dangling pointer 방지).
 */
class Channel
{
private:
	std::string             _name;
	std::string             _topic;
	std::string             _topicSetter;
	std::time_t             _topicTime;
	std::time_t             _creationTime;

	std::string             _key;              // +k (빈 문자열이면 비활성)
	size_t                  _limit;            // +l (0이면 비활성)
	bool                    _inviteOnly;       // +i
	bool                    _topicRestricted;  // +t

	std::vector<Client*>    _members;          // 채널 멤버 목록
	std::vector<Client*>    _operators;        // 채널 운영자(@) 목록 (반드시 _members의 부분집합)
	std::set<std::string>   _invited;          // 초대된 닉네임 (ircToLower 정규화)

	Channel();
	Channel(const Channel& other);
	Channel& operator=(const Channel& other);

public:
	explicit Channel(const std::string& name);
	~Channel();

	// ---------- 기본 정보 ----------
	const std::string&          getName() const;
	std::time_t                 getCreationTime() const;

	// ---------- 토픽 ----------
	const std::string&          getTopic() const;
	const std::string&          getTopicSetter() const;
	std::time_t                 getTopicTime() const;
	void                        setTopic(const std::string& topic, const std::string& setter);

	// ---------- 모드 ----------
	bool                        isInviteOnly() const;
	void                        setInviteOnly(bool enabled);
	bool                        isTopicRestricted() const;
	void                        setTopicRestricted(bool enabled);

	bool                        hasKey() const;
	const std::string&          getKey() const;
	void                        setKey(const std::string& key);
	void                        clearKey();

	bool                        hasLimit() const;
	size_t                      getLimit() const;
	void                        setLimit(size_t limit);
	bool                        isFull() const;

	std::string                 getModeString(bool includeParams) const;

	// ---------- 멤버 ----------
	const std::vector<Client*>& getMembers() const;
	size_t                      getMemberCount() const;
	bool                        isEmpty() const;
	bool                        hasMember(const Client* client) const;
	void                        addMember(Client* client);
	void                        removeMember(Client* client);
	std::string                 getNamesList() const;

	// ---------- 운영자 ----------
	bool                        isOperator(const Client* client) const;
	void                        addOperator(Client* client);
	void                        removeOperator(Client* client);

	// ---------- 초대 목록 ----------
	bool                        isInvited(const std::string& nickname) const;
	void                        addInvite(const std::string& nickname);
	void                        removeInvite(const std::string& nickname);
	void                        renameInvite(const std::string& oldNick, const std::string& newNick);
};

#endif