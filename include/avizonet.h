#ifndef AVIZONET_H
#define AVIZONET_H

#include <atomic>
#include <cstdint>
#include <functional>

namespace AvizoNet
{

using InterruptHandlerFn = std::function<void()>;
bool SetInterruptHandler(InterruptHandlerFn handler);

class Context
{
public:
    Context();
    ~Context();

    Context(Context&) = delete;
    Context& operator=(Context&) = delete;

    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

private:
    static std::atomic<int> _numInstances;
};

struct Address
{
    uint8_t octets[4];
    uint16_t port;
};

enum class ReceivingState
{
    ReceivingHeader,
    ReceivingBody
};

struct Client
{
    bool connected;
    uint64_t socket;
    Address address;
    
    ReceivingState receivingState;
    std::vector<uint8_t> receivingBuffer;
    uint32_t bytesRemaining;

    uint32_t incomingPacketId;
    uint16_t incomingPacketSize;
};

class TcpClient
{
public:
    TcpClient(const Context& context, const Address& serverAddress);
    ~TcpClient();

    TcpClient(TcpClient&) = delete;
    TcpClient& operator=(TcpClient&) = delete;

    TcpClient(TcpClient&&) = delete;
    TcpClient& operator=(TcpClient&&) = delete;

    void Run();

    void RequestShutdown() { _shutdownRequested = true; }
    bool ShutdownRequested() const { return _shutdownRequested; }

protected:
    virtual void OnConnect() {}
    virtual void OnDisconnect() {}
    virtual void OnRawDataReceive(const uint8_t* buffer, uint32_t size);
    virtual void OnPacketReceive(uint32_t packetId, 
            std::vector<uint8_t>&& body) {}
    
    void SendRawData(const uint8_t* buffer, uint32_t size);
    void SendPacket(uint32_t packetId, const std::vector<uint8_t>& body);

private:
    uint64_t _clientSocket;
    bool _shutdownRequested = false;

    ReceivingState _receivingState;
    std::vector<uint8_t> _receivingBuffer;
    uint32_t _bytesRemaining;

    uint32_t _incomingPacketId;
    uint16_t _incomingPacketSize;
};

class TcpServer
{
public:
    TcpServer(const Context& context, uint16_t port, uint32_t maxClients);
    ~TcpServer();

    TcpServer(TcpServer&) = delete;
    TcpServer& operator=(TcpServer&) = delete;

    TcpServer(TcpServer&&) = delete;
    TcpServer& operator=(TcpServer&&) = delete;

    void Run();

    void RequestShutdown();
    bool ShutdownRequested() const { return _shutdownRequested; }

protected:
    virtual void OnClientConnect(uint32_t id) {}
    virtual void OnRawDataReceive(
            uint32_t id, const uint8_t* buffer, uint32_t size);
    virtual void OnClientDisconnect(uint32_t id) {}
    virtual void OnPacketReceive(uint32_t clientId, uint32_t packetId, 
            std::vector<uint8_t>&& body) {}

    void SendRawData(uint32_t id, const uint8_t* buffer, uint32_t size);
    void SendPacket(uint32_t clientId, uint32_t packetId, 
            const std::vector<uint8_t>& body);

    std::vector<Client> _clients;

private:
    const Context& _context;
    uint64_t _serverSocket;
    bool _shutdownRequested = false;
};

}

#endif
