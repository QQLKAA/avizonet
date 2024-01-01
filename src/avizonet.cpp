#include <avizonet.h>

#include <cerealizer.h>

#include <iostream>
#include <WinSock2.h>

namespace AvizoNet
{

static InterruptHandlerFn gInterruptHandler = nullptr;

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT)
    {
        if (gInterruptHandler)
        {
            gInterruptHandler();
            return TRUE;
        }
    }
    return FALSE;
}

bool SetInterruptHandler(InterruptHandlerFn handler)
{
    gInterruptHandler = handler;

    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE))
    {
        return false;
    }

    return true;
}

std::atomic<int> Context::_numInstances = 0;

Context::Context()
{
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        throw std::runtime_error("WSAStartup failed");
    }
    _numInstances++;
}

Context::~Context()
{
    if (--_numInstances <= 0)
    {
        WSACleanup();
    }
}

TcpServer::TcpServer(const Context &context, uint16_t port, uint32_t maxClients)
    : _context(context), _clients(maxClients)
{
    _serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_serverSocket == INVALID_SOCKET)
    {
        throw std::runtime_error("socket() failed");
    }

    sockaddr_in address{};
    address.sin_addr.S_un.S_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (bind(_serverSocket, reinterpret_cast<sockaddr*>(&address), 
            sizeof(address)) == SOCKET_ERROR)
    {
        throw std::runtime_error("bind() failed");
    }

    if (listen(_serverSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        throw std::runtime_error("listen() failed");
    }

    u_long mode = 1; // non-blocking
    if (ioctlsocket(_serverSocket, FIONBIO, &mode) != 0)
    {
        throw std::runtime_error("ioctlsocket() failed");
    }
}

TcpServer::~TcpServer()
{
    closesocket(_serverSocket);
}

void TcpServer::Run()
{
    FD_SET fds;

    while (!_shutdownRequested)
    {
        FD_ZERO(&fds);
        FD_SET(_serverSocket, &fds);

        for (Client& client : _clients)
        {
            if (client.connected)
                FD_SET(client.socket, &fds);
        }

        // 100ms
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int selectResult = select(0, &fds, nullptr, nullptr, &timeout);
        if (selectResult == -1)
        {
            throw std::runtime_error("select() failed");
        }
        else if (selectResult == 0)
        {
            // timeout
        }
        else
        {
            if (FD_ISSET(_serverSocket, &fds))
            {
                sockaddr_in clientAddr;
                int clientAddrSize = sizeof(clientAddr);

                uint64_t clientSocket = accept(_serverSocket, 
                        reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrSize);
                if (clientSocket == INVALID_SOCKET)
                {
                    throw std::runtime_error("accept() failed");
                }


                auto it = std::find_if(_clients.begin(), _clients.end(), 
                        [](const Client& client)
                        { return client.connected == false; });
                
                if (it == _clients.end())
                {
                    char buffer[] = "Server is full!\r\n";
                    int bufferLength = strlen(buffer);
                    if (send(clientSocket, buffer, bufferLength, 0) != bufferLength)
                    {
                        throw std::runtime_error("send() failed");
                    }

                    if (shutdown(clientSocket, SD_SEND) == SOCKET_ERROR)
                    {
                        throw std::runtime_error("shutdown() failed");
                    }

                    closesocket(clientSocket);
                }
                else
                {
                    uint32_t id = std::distance(_clients.begin(), it);

                    Client& client = _clients[id];

                    client.address.octets[0] = clientAddr.sin_addr.S_un.S_un_b.s_b1;
                    client.address.octets[2] = clientAddr.sin_addr.S_un.S_un_b.s_b2;
                    client.address.octets[3] = clientAddr.sin_addr.S_un.S_un_b.s_b3;
                    client.address.octets[4] = clientAddr.sin_addr.S_un.S_un_b.s_b4;
                    client.address.port = ntohs(clientAddr.sin_port);

                    client.connected = true;
                    client.socket = clientSocket;

                    OnClientConnect(id);
                }
            }

            for (uint32_t clientId = 0; clientId < _clients.size(); clientId++)
            {
                auto& client = _clients[clientId];

                if (!client.connected)
                    continue;

                if (!FD_ISSET(client.socket, &fds))
                    continue;
                
                uint8_t buffer[1024];
                int received = recv(client.socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
                if (received == SOCKET_ERROR)
                {
                    throw std::runtime_error("recv failed");
                }

                else if (received == 0)
                {
                    client.connected = false;
                    OnClientDisconnect(clientId);
                }
                else
                {
                    OnRawDataReceive(clientId, buffer, static_cast<uint32_t>(received));
                }
            }
        }
    }
}

void TcpServer::RequestShutdown()
{
    _shutdownRequested = true;
}


void TcpServer::SendRawPacket(uint32_t id, const uint8_t *buffer, uint32_t size)
{
    Client& client = _clients[id];
    int remaining = static_cast<int>(size);
    
    while (remaining > 0)
    {
        int sent = send(client.socket, 
                reinterpret_cast<const char*>(buffer + size - remaining - 1), 
                remaining, 0);
        if (sent == SOCKET_ERROR)
        {
            throw std::runtime_error("send failed");
        }

        remaining -= sent;
    }
}

}
