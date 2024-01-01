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

struct ClientAddress
{
    uint8_t octets[4];
    uint16_t port;
};

struct Client
{
    bool connected;
    uint64_t socket;
    ClientAddress address;
};

class TcpServer
{
public:
    TcpServer(const Context& context, uint16_t port, uint32_t maxClients);
    ~TcpServer();

    void Run();

    void RequestShutdown();
    bool ShutdownRequested() const { return _shutdownRequested; }

protected:
    virtual void OnClientConnect(uint32_t id) {}
    virtual void OnRawDataReceive(uint32_t id, const uint8_t* buffer, uint32_t size) {}
    virtual void OnClientDisconnect(uint32_t id) {}

    void SendRawPacket(uint32_t id, const uint8_t* buffer, uint32_t size);

    std::vector<Client> _clients;

private:
    const Context& _context;
    uint64_t _serverSocket;
    bool _shutdownRequested = false;
};

}

#endif
