#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <vector>

class Client;

class Channel 
{
public:
    Channel();
    ~Channel();

    std::string          name;
    std::vector<Client*> members;
    std::vector<Client*> operators;
    std::string          topic;
    std::string          key;
    bool                 inviteOnly;
    bool                 topicRestricted;
    int                  limit;

    // 채널의 이름을 설정합니다.
    void setName(const std::string& newName);

    // 현재 채널의 이름을 반환합니다.
    const std::string& getName() const;

    // Client가 현재 채널의 멤버인지 확인합니다.
    bool isMember(Client* client) const;
    
    // Client가 현재 채널의 관리자인지 확인합니다.
    bool isOperator(Client* client) const;

    // Client를 현재 채널의 멤버에 추가합니다.
    void addMember(Client* client);

    // Client를 현재 채널의 멤버에서 삭제합니다.
    void removeMember(Client* client);

    // Client를 현재 채널의 관리자에 추가합니다.
    void addOperator(Client* client);

    // Client를 현재 채널의 관리자에서 삭제합니다.
    void removeOperator(Client* client);

    // 채널의 topic을 변경합니다.
    void setTopic(const std::string& newTopic);

    // 현재 채널의 topic을 반환합니다.
    const std::string& getTopic() const;

    // 채널의 key를 설정합니다.
    void setKey(const std::string& newkey);

    // 채널의 key를 반환합니다.
    const std::string& getKey() const;

    // 채널의 key를 제거합니다.
    void clearKey();

    // 채널의 invite-only 모드를 설정합니다.
    void setInviteOnly(bool value);

    // 채널이 invite-only 상태인지 확인합니다.
    bool isInviteOnly() const;

    // 채널의 topic 변경 제한 모드를 설정합니다.
    void setTopicRestricted(bool value);

    // 채널의 topic 변경 제한 여부를 확인합니다.
    bool isTopicRestricted() const;

    // 채널의 최대 인원 수를 설정합니다.
    void setLimit(int newLimit);

    // 현재 채널의 최대 인원 수를 반환합니다.
    int getLimit() const;

    // 채널의 최대 인원 제한을 해제합니다.
    void clearLimit();
};

#endif