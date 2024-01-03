#include "common.h"

#include <avizonet.h>

#include <iostream>
#include <thread>

class ChatClient : public AvizoNet::TcpClient
{
public:
    ChatClient(
            const AvizoNet::Context& context, 
            const AvizoNet::Address& serverAddress)
        : AvizoNet::TcpClient(context, serverAddress)
    {
    }

    void OnConnect() override
    {
        AvizoNet::TcpClient::OnConnect();
        std::puts("Connected!");
    }

    void OnDisconnect() override
    {
        AvizoNet::TcpClient::OnDisconnect();
        std::puts("Disconnected!");
    }

    void SendMessage(const std::string& content)
    {
        MessagePacket msg;
        msg.content = content;

        Cerealizer::Context ctx;
        ctx.Serialize(msg);
        SendPacket(ChatPacketId::C_SendMessage, ctx.GetBuffer());
    }

    void OnPacketReceive(
            uint32_t packetId, std::vector<uint8_t>&& body) override
    {
        Cerealizer::Context ctx(body);
        switch (packetId)
        {
        case ChatPacketId::S_BroadcastMessage:
        {
            BroadcastMessagePacket bdcMsg;
            if (!ctx.Deserialize(bdcMsg))
                throw std::runtime_error("cannot deserialize bdcmsg");
            std::printf("[%s]: %s\n", 
                    bdcMsg.nickname.c_str(), bdcMsg.content.c_str());
        } break;
        case ChatPacketId::S_RequestLogin:
        {
            RequestLoginPacket reqLogin;
            if (!ctx.Deserialize(reqLogin))
                throw std::runtime_error("cannot deserialize req login");

            std::printf("Login requested: %s\nProvide your nickname: ",
                    reqLogin.message.c_str());
            std::string nickname;
            std::getline(std::cin, nickname);

            LoginPacket login;
            login.nickname = nickname;
            ctx.ClearBuffer();
            ctx.Serialize(login);
            SendPacket(ChatPacketId::C_Login, ctx.GetBuffer());
        } break;
        case ChatPacketId::S_CanChat:
        {
            std::printf("You can chat now.");
            std::thread([&]()
            {
                while (!ShutdownRequested())
                {
                    std::string line;
                    std::getline(std::cin, line);
                    SendMessage(line);
                }
            }).detach();
        } break;
        default:
            throw std::runtime_error("invalid packet id");
        }
    }
};

int main(int argc, char **argv)
{
    try 
    {
        AvizoNet::Context networkContext;
        AvizoNet::Address serverAddress;
        serverAddress.octets[0] = 127;
        serverAddress.octets[1] = 0;
        serverAddress.octets[2] = 0;
        serverAddress.octets[3] = 1;
        serverAddress.port = 1234;
        ChatClient client(networkContext, serverAddress);

        AvizoNet::SetInterruptHandler([&client]()
        {
            if (!client.ShutdownRequested())
            {
                client.RequestShutdown();
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

        /*std::thread senderThread([&]()
        {
            while (!client.ShutdownRequested())
            {
                std::string line;
                std::getline(std::cin, line);
                client.SendMessage(line);
            }
        });*/

        client.Run();
        //senderThread.join();

        AvizoNet::SetInterruptHandler(nullptr);
    }
    catch(const std::exception& e)
    {
        std::cerr << "Exception occured: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
