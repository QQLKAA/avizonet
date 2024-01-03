#include <avizonet.h>

#include <format>
#include <iostream>

class HelloWorldServer : public AvizoNet::TcpServer
{
public:
    HelloWorldServer(const AvizoNet::Context& context)
        : AvizoNet::TcpServer(context, 1234, 256) {}

    virtual void OnClientConnect(uint32_t id) override
    {
        AvizoNet::TcpServer::OnClientConnect(id);

        const auto& addr = _clients[id].address;
        std::printf("Client #%d connected from %d.%d.%d.%d:%d\n",
                id, 
                addr.octets[0], addr.octets[1], addr.octets[2], addr.octets[3], 
                addr.port);

        std::string message = "hello, world!\n";
        SendRawData(id, (const uint8_t*)message.c_str(), message.size());
    }

    virtual void OnClientDisconnect(uint32_t id) override
    {
        AvizoNet::TcpServer::OnClientDisconnect(id);
        std::printf("Client #%d disconnected\n", id);
    }

    virtual void OnRawDataReceive(
            uint32_t id, const uint8_t* buffer, uint32_t size) override
    {
        AvizoNet::TcpServer::OnRawDataReceive(id, buffer, size);
        std::printf("Client #%d sent %d bytes\n", id, size);

        for (uint32_t i = 0; i < _clients.size(); i++)
        {
            if (i == id || !_clients[i].connected)
                continue;

            SendRawData(i, buffer, size);
        }
    }

    virtual void OnPacketReceive(uint32_t clientId, uint32_t packetId,
            std::vector<uint8_t>&& body)
    {
        std::string message(body.begin(), body.end());
        std::printf("Client #%d sent packet of id 0x%04X, length %d and content %s\n", clientId, packetId, body.size(), message.c_str());
    }
};

int main()
{
    try 
    {
        AvizoNet::Context networkContext;
        HelloWorldServer server(networkContext);

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
