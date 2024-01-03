#include "common.h"

#include <avizonet.h>

#include <iostream>

class ChatServer : public AvizoNet::TcpServer
{
public:
    struct ClientContext
    {
        std::string nickname;
        bool isLoggedIn;
    };

    ChatServer(const AvizoNet::Context& context)
        : AvizoNet::TcpServer(context, 1234, 256), _clientCtxs(256)
    {}

    void BroadcastMessage(
            const std::string& nickname, const std::string& content)
    {
        BroadcastMessagePacket bdcMsg;
        bdcMsg.nickname = nickname;
        bdcMsg.content = content;
        Cerealizer::Context ctx;
        ctx.Serialize(bdcMsg);

        for (uint32_t i = 0; i < _clients.size(); i++)
        {
            if (!_clients[i].connected || !_clientCtxs[i].isLoggedIn)
                continue;
            
            SendPacket(i, ChatPacketId::S_BroadcastMessage, ctx.GetBuffer());
        }
    }

    void OnClientConnect(uint32_t id) override
    {
        std::printf("Client #%d connected\n", id);

        _clientCtxs[id].isLoggedIn = false;

        BroadcastMessagePacket bdcMsg;
        bdcMsg.nickname = "Server Info";
        bdcMsg.content = "MOTD: Hello World!";
        Cerealizer::Context ctx;
        ctx.Serialize(bdcMsg);
        SendPacket(id, ChatPacketId::S_BroadcastMessage, ctx.GetBuffer());

        RequestLoginPacket reqLogin;
        reqLogin.message = "Welcome! You must login to continue!";
        ctx.ClearBuffer();
        ctx.Serialize(reqLogin);
        SendPacket(id, ChatPacketId::S_RequestLogin, ctx.GetBuffer());
    }

    void OnClientDisconnect(uint32_t id) override
    {
        std::printf("Client #%d disconnected\n", id);
    }

    void OnPacketReceive(uint32_t clientId, uint32_t packetId, 
            std::vector<uint8_t>&& body)
    {
        Cerealizer::Context ctx(body);

        switch (packetId)
        {
        case ChatPacketId::C_SendMessage:
        {
            if (!_clientCtxs[clientId].isLoggedIn)
            {
                RequestLoginPacket reqLogin;
                reqLogin.message = "You must login before chatting";
                ctx.ClearBuffer();
                ctx.Serialize(reqLogin);
                SendPacket(clientId, ChatPacketId::S_RequestLogin, ctx.GetBuffer());
                break;
            }

            MessagePacket msg;
            if (!ctx.Deserialize(msg))
                throw std::runtime_error("Cannot deserialize message packet");

            BroadcastMessage(_clientCtxs[clientId].nickname, msg.content);
        } break;

        case ChatPacketId::C_Login:
        {
            if (_clientCtxs[clientId].isLoggedIn)
                break;
            
            LoginPacket login;
            if (!ctx.Deserialize(login))
                throw std::runtime_error("cant deserialize login packet");
            
            bool nicknameAvailable = true;
            for (uint32_t i = 0; i < _clients.size(); i++)
            {
                if (!_clients[i].connected)
                    continue;

                if (!_clientCtxs[i].isLoggedIn)
                    continue;

                if (_clientCtxs[i].nickname == login.nickname)
                {
                    nicknameAvailable = false;
                    break;
                }
            }

            if (!nicknameAvailable)
            {
                std::printf("Client #%d requested login with nickname '%s', "
                        "but it's already taken\n",
                        clientId, login.nickname.c_str());
            
                RequestLoginPacket reqLogin;
                reqLogin.message = "This nickname is already taken. Please choose another.";
                ctx.ClearBuffer();
                ctx.Serialize(reqLogin);
                SendPacket(clientId, ChatPacketId::S_RequestLogin, ctx.GetBuffer());
                break;
            }

            std::printf("Client #%d requested login with nickname '%s', "
                    "login successfull\n",
                    clientId, login.nickname.c_str());

            _clientCtxs[clientId].isLoggedIn = true;
            _clientCtxs[clientId].nickname = login.nickname;

            ctx.ClearBuffer();
            SendPacket(clientId, ChatPacketId::S_CanChat, ctx.GetBuffer());
        } break;
        
        default:
            throw std::runtime_error("invalid packet id");
        }
    }

    std::vector<ClientContext> _clientCtxs;
};

int main()
{
    try 
    {
        AvizoNet::Context networkContext;
        ChatServer server(networkContext);

        AvizoNet::SetInterruptHandler([&server]()
        {
            if (!server.ShutdownRequested())
            {
                server.RequestShutdown();
                std::cout 
                        << "Shutdown requested. "
                        << "Press Ctrl-C again to force quit." 
                        << std::endl;
            }
            else
            {
                std::cout << "Quitting forcefully." << std::endl;
                std::exit(0);
            }
        });

        server.Run();

        AvizoNet::SetInterruptHandler(nullptr);
    }
    catch(const std::exception& e)
    {
        std::cerr << "Exception occured: " << e.what() << std::endl;
        return 1;
    }

    return 0;   
}
