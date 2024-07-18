#pragma once
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <functional>
#include <unordered_map>
#include <boost/asio.hpp>

using namespace boost;

class FRpcServer;

class FTcpSocket : std::enable_shared_from_this<FTcpSocket> {
    public:
    using Pointer = std::shared_ptr<FTcpSocket>;
    using WeakPtr = std::weak_ptr<FTcpSocket>;
    FTcpSocket(FRpcServer& rpcServer, asio::ip::tcp::socket socket, uint64_t endpointId);

    ~FTcpSocket();

    asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpSocket> self);
protected:
    FRpcServer& RpcServer;
    uint64_t EndpointId;
    asio::ip::tcp::socket Socket;
};

struct FTcpConnection {
    asio::ip::tcp::endpoint RemoteEndpoint;
    FTcpSocket::Pointer SocketPointer;
    FTcpSocket::WeakPtr SocketWeakPtr;
};

class FRpcServer : public std::enable_shared_from_this<FRpcServer>
{
public:
    FRpcServer(std::shared_ptr<asio::io_context> ioContext, asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        : IoContext(std::move(ioContext))
        , IoStrand(asio::make_strand(*IoContext))
        , Acceptor(*IoContext, asio::ip::tcp::endpoint(address, port))
    {

    }

    void Listen() {
        asio::co_spawn(IoStrand, Listen(shared_from_this()), asio::detached);
    }

protected:
    asio::awaitable<void> Listen(std::shared_ptr<FRpcServer> self) {
        auto executor = asio::bind_executor(IoStrand, asio::use_awaitable);
        try
        {
            for (;;)
            {
                asio::ip::tcp::endpoint remoteEndpoint;
                asio::ip::tcp::socket socket = co_await Acceptor.async_accept(remoteEndpoint, asio::use_awaitable);
                uint64_t endpointId = remoteEndpoint.address().to_v4().to_uint();
                endpointId = (endpointId << 16) + remoteEndpoint.port();
                std::shared_ptr<FTcpSocket> tcpSocket = std::make_shared<FTcpSocket>(*this, std::move(socket), endpointId);
                if (!remoteEndpoint.address().is_v4())
                {
                    continue;
                }
                std::string ip = remoteEndpoint.address().to_string();
                asio::ip::port_type port = remoteEndpoint.port();
                co_await asio::post(executor);
                auto insertResult = ConnectionMap.insert(std::pair(endpointId, FTcpConnection{
                    .RemoteEndpoint = remoteEndpoint,
                    .SocketPointer = tcpSocket,
                    .SocketWeakPtr = tcpSocket,
                }));
                if (!insertResult.second)
                {
                    continue;
                }
                std::cout << "conn[" << ip << ":" << port << "]: connected" << std::endl;
                asio::co_spawn(*IoContext, tcpSocket->AsyncRead(std::move(tcpSocket)), asio::detached);
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "exception: " << e.what() << std::endl;
        }
    }

    void ReleaseConnection(uint64_t endpointId)
    {
        auto it = ConnectionMap.find(endpointId);
        if (it != ConnectionMap.end())
        {
            auto& endpoint = it->second.RemoteEndpoint;
            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: disconnected" << std::endl;
            ConnectionMap.erase(it);
        }
    }

protected:
    std::shared_ptr<asio::io_context> IoContext;
    asio::ip::tcp::acceptor Acceptor;

    asio::strand<asio::io_context::executor_type> IoStrand;
    std::map<uint64_t, FTcpConnection> ConnectionMap;

    friend class FTcpSocket;
};

inline FTcpSocket::FTcpSocket(FRpcServer& rpcServer, asio::ip::tcp::socket socket, uint64_t endpointId)
    : RpcServer(rpcServer)
    , Socket(std::move(socket))
    , EndpointId(endpointId)
{

}

inline FTcpSocket::~FTcpSocket() {
    RpcServer.ReleaseConnection(EndpointId);
}

inline asio::awaitable<void> FTcpSocket::AsyncRead(std::shared_ptr<FTcpSocket> self)
{
    char buffer[4 * 1024];
    try
    {
        for (;;)
        {
            auto bytesTransferred = co_await Socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
            bytesTransferred = co_await Socket.async_write_some(asio::buffer(buffer, bytesTransferred), asio::use_awaitable);
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "exception: " << e.what() << std::endl;
    }
    co_await asio::post(asio::bind_executor(RpcServer.IoStrand, asio::use_awaitable));
    RpcServer.ConnectionMap[EndpointId].SocketPointer.reset();
}


//
//class FRpcDispatcher
//{
//public:
//    FRpcDispatcher() {
//
//    }
//
//    void Call(std::string name, std::vector<uint8_t> serializedParameters)
//    {
//        auto it = FuncMap.find(name);
//        if (1)
//        {
//            /* code */
//        }
//
//
//    }
//
//protected:
//    std::unordered_map<std::string, std::function<void()>> FuncMap;
//
//};
// 
//
//class FRpcClient
//{
//public:
//    FRpcClient(std::string ip, uint16_t port)
//    {
//        
//    }
//
//
//    void AsyncCall() {
//
//    }
//
//protected:
//    FConnectionData ConnectionData;
//
//};

