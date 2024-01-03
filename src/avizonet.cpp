#include <avizonet.h>

#include <cerealizer.h>

#include <algorithm>
#include <iostream>

#define NOMINMAX
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
                        reinterpret_cast<sockaddr*>(&clientAddr), 
                        &clientAddrSize);
                if (clientSocket == INVALID_SOCKET)
                {
                    throw std::runtime_error("accept() failed");
                }


                auto it = std::find_if(_clients.begin(), _clients.end(), 
                        [](const Client& client)
                        { return client.connected == false; });
                
                if (it == _clients.end())
                {
                    // TODO: Make proper handling of rejecting connection

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

                    client.receivingBuffer.clear();
                    client.receivingState = ReceivingState::ReceivingHeader;
                    client.bytesRemaining = 6;

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
                int toReceive = std::min(static_cast<int>(sizeof(buffer)), static_cast<int>(client.bytesRemaining));
                int received = recv(client.socket, reinterpret_cast<char*>(buffer), toReceive, 0);
                if (received == SOCKET_ERROR)
                {
                    printf("recv wsa error: %d\n", WSAGetLastError());
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


void TcpServer::SendRawData(uint32_t id, const uint8_t *buffer, uint32_t size)
{
    Client& client = _clients[id];
    int remaining = static_cast<int>(size);

    assert(client.connected == true);
    
    while (remaining > 0)
    {
        int sent = send(client.socket, 
                reinterpret_cast<const char*>(&buffer[size - remaining]), 
                remaining, 0);
        if (sent == SOCKET_ERROR)
        {
            throw std::runtime_error("send failed");
        }

        remaining -= sent;
    }
}

void TcpServer::SendPacket(
        uint32_t clientId, uint32_t packetId, const std::vector<uint8_t>& body)
{
    assert(body.size() <= std::numeric_limits<uint16_t>::max());

    uint8_t header[6];
    *reinterpret_cast<uint32_t*>(header) = packetId;
    *reinterpret_cast<uint16_t*>(header + 4) = body.size();
    SendRawData(clientId, header, sizeof(header));

    SendRawData(clientId, body.data(), body.size());
}

void TcpServer::OnRawDataReceive(
        uint32_t id, const uint8_t *buffer, uint32_t size)
{
    Client& client = _clients[id];

    assert(size <= client.bytesRemaining);

    client.receivingBuffer.insert(
            client.receivingBuffer.end(), buffer, buffer + size);
    client.bytesRemaining -= size;

    if (client.bytesRemaining == 0)
    {
        switch (client.receivingState)
        {
        case ReceivingState::ReceivingHeader:
        {
            assert(client.receivingBuffer.size() == 6);

            uint32_t packetId = *reinterpret_cast<uint32_t*>(client.receivingBuffer.data());
            uint16_t packetLength = *reinterpret_cast<uint32_t*>(client.receivingBuffer.data() + 4);

            if (packetLength > 0)
            {
                client.receivingBuffer.clear();
                client.bytesRemaining = packetLength;
                client.receivingState = ReceivingState::ReceivingBody;
                client.incomingPacketId = packetId;
                client.incomingPacketSize = packetLength;
            }
            else
            {
                OnPacketReceive(id, packetId, std::vector<uint8_t>());
                client.receivingBuffer.clear();
                client.bytesRemaining = 6;
                client.receivingState = ReceivingState::ReceivingHeader;
            }
        } break;
        case ReceivingState::ReceivingBody:
        {
            assert(client.receivingBuffer.size() == client.incomingPacketSize);
            
            OnPacketReceive(id, client.incomingPacketId, std::move(client.receivingBuffer));

            client.receivingBuffer = std::vector<uint8_t>();
            client.bytesRemaining = 6;
            client.receivingState = ReceivingState::ReceivingHeader;
        } break;
        default:
            assert(false);
        }
    }
}

TcpClient::TcpClient(const Context &context, const Address &serverAddress)
{
    _clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_clientSocket == INVALID_SOCKET)
    {
        throw std::runtime_error("socket() failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.S_un.S_un_b.s_b1 = serverAddress.octets[0];
    addr.sin_addr.S_un.S_un_b.s_b2 = serverAddress.octets[1];
    addr.sin_addr.S_un.S_un_b.s_b3 = serverAddress.octets[2];
    addr.sin_addr.S_un.S_un_b.s_b4 = serverAddress.octets[3];
    addr.sin_port = htons(serverAddress.port);

    int result = connect(_clientSocket, 
            reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (result == SOCKET_ERROR)
    {
        throw std::runtime_error("connect() failed");
    }

    u_long mode = 1; // non-blocking
    if (ioctlsocket(_clientSocket, FIONBIO, &mode) != 0)
    {
        throw std::runtime_error("ioctlsocket() failed");
    }

    _receivingBuffer.clear();
    _receivingState = ReceivingState::ReceivingHeader;
    _bytesRemaining = 6;
}

TcpClient::~TcpClient()
{
    closesocket(_clientSocket);
}

void TcpClient::Run()
{
    OnConnect();

    fd_set fds;
    while (!_shutdownRequested)
    {
        FD_ZERO(&fds);
        FD_SET(_clientSocket, &fds);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        int selectResult = select(0, &fds, nullptr, nullptr, &timeout);
        if (selectResult == -1)
        {
            throw std::runtime_error("Select failed");
        }
        else if (selectResult == 0)
        {
            // timeout
        }
        else
        {
            if (!FD_ISSET(_clientSocket, &fds))
                continue;

            uint8_t buffer[1024];
            int toReceive = std::min(
                    static_cast<int>(sizeof(buffer)), 
                    static_cast<int>(_bytesRemaining));
            int received = recv(_clientSocket, 
                    reinterpret_cast<char*>(buffer), toReceive, 0);
            if (received == SOCKET_ERROR)
            {
                printf("recv wsa error: %d\n", WSAGetLastError());
                throw std::runtime_error("recv failed");
            }
            else if (received == 0)
            {
                OnDisconnect();
                break;
            }
            else
            {
                OnRawDataReceive(buffer, static_cast<uint32_t>(received));
            }
        }
    }
}

void TcpClient::OnRawDataReceive(const uint8_t* buffer, uint32_t size)
{
    assert(size <= _bytesRemaining);

    _receivingBuffer.insert(_receivingBuffer.end(), buffer, buffer + size);
    _bytesRemaining -= size;

    if (_bytesRemaining == 0)
    {
        switch (_receivingState)
        {
        case ReceivingState::ReceivingHeader:
        {
            assert(_receivingBuffer.size() == 6);

            uint32_t packetId = *reinterpret_cast<uint32_t*>(_receivingBuffer.data());
            uint16_t packetLength = *reinterpret_cast<uint32_t*>(_receivingBuffer.data() + 4);

            if (packetLength > 0)
            {
                _receivingBuffer.clear();
                _bytesRemaining = packetLength;
                _receivingState = ReceivingState::ReceivingBody;
                _incomingPacketId = packetId;
                _incomingPacketSize = packetLength;
            }
            else
            {
                OnPacketReceive(packetId, std::vector<uint8_t>());
                _receivingBuffer.clear();
                _bytesRemaining = 6;
                _receivingState = ReceivingState::ReceivingHeader;
            }
        } break;
        case ReceivingState::ReceivingBody:
        {
            assert(_receivingBuffer.size() == _incomingPacketSize);
            
            OnPacketReceive(_incomingPacketId, std::move(_receivingBuffer));

            _receivingBuffer = std::vector<uint8_t>();
            _bytesRemaining = 6;
            _receivingState = ReceivingState::ReceivingHeader;
        } break;
        default:
            assert(false);
        }
    }

}

void TcpClient::SendRawData(const uint8_t* buffer, uint32_t size)
{
    int remaining = static_cast<int>(size);

    while (remaining > 0)
    {
        int sent = send(_clientSocket, 
                reinterpret_cast<const char*>(&buffer[size - remaining]), 
                remaining, 0);
        if (sent == SOCKET_ERROR)
        {
            throw std::runtime_error("send failed");
        }

        remaining -= sent;
    }

}

void TcpClient::SendPacket(uint32_t packetId, const std::vector<uint8_t>& body)
{
    assert(body.size() <= std::numeric_limits<uint16_t>::max());

    uint8_t header[6];
    *reinterpret_cast<uint32_t*>(&header[0]) = packetId;
    *reinterpret_cast<uint16_t*>(&header[4]) = body.size();
    SendRawData(header, sizeof(header));

    SendRawData(body.data(), body.size());
}

}
