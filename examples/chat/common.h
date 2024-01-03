#ifndef CHAT_COMMON_H
#define CHAT_COMMON_H

#include <cerealizer.h>

namespace ChatPacketId
{
    enum ChatPacketId : uint32_t
    {
        C_SendMessage,
        S_BroadcastMessage,
        C_Login,
        S_RequestLogin,
        S_CanChat,
    };
}

#define SERIALIZABLE(...) \
    void Serialize(Cerealizer::Context& context) const { context.Serialize(__VA_ARGS__); } \
    bool Deserialize(Cerealizer::Context& context) { return context.Deserialize(__VA_ARGS__); }

struct MessagePacket
{
    std::string content;
    SERIALIZABLE(content);
};

struct BroadcastMessagePacket
{
    std::string nickname, content;
    SERIALIZABLE(nickname, content);
};

struct RequestLoginPacket
{
    std::string message;
    SERIALIZABLE(message);
};

struct LoginPacket
{
    std::string nickname;
    SERIALIZABLE(nickname);
};

#endif
