#pragma once
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <queue>
#include <iostream>
#include <functional>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>

//struct FConnectionProxy {
//    std::function<void()> OnConnected;
//    std::function<void()> OnRecvData;
//    std::function<void()> OnDisconnected;
//};

template <typename T>
inline T EndianCast(const T& val)
{
    if constexpr (std::endian::native == std::endian::little) {
        return boost::endian::endian_reverse(val);
    }
    return val;
}

using namespace boost;

class FTcpSocket;

struct ITcpService : public std::enable_shared_from_this<ITcpService>
{
    ITcpService(asio::io_context& ioContext)
        : IoContext(ioContext)
        , ServiceStrand(asio::make_strand(ioContext))
    {}

    virtual bool Acquire(FTcpSocket* tcpSocket) = 0;
    virtual void Release(FTcpSocket* tcpSocket) = 0;
    virtual auto AsyncInsertConnection(asio::ip::tcp::socket socket, asio::strand<asio::io_context::executor_type> ServiceStrand) -> asio::awaitable<std::shared_ptr<FTcpSocket>> = 0;
    virtual auto AsyncRemoveConnection(uint64_t connectionId) -> asio::awaitable<void> = 0;

protected:
    asio::io_context& IoContext;
    asio::strand<asio::io_context::executor_type> ServiceStrand;

    friend class FTcpSocket;
};

class FRpcService;

class FTcpSocket : public std::enable_shared_from_this<FTcpSocket> {
public:
    FTcpSocket(ITcpService* tcpService, uint64_t connectionId, asio::ip::tcp::socket socket, asio::strand<asio::io_context::executor_type> socketStrand)
        : TcpService(tcpService)
        , ConnectionId(connectionId)
        , Socket(std::move(socket))
        , SocketStrand(socketStrand)
    {}
    ~FTcpSocket() { asio::co_spawn(TcpService->ServiceStrand, TcpService->AsyncRemoveConnection(ConnectionId), asio::detached); }
    void Read() { asio::co_spawn(SocketStrand, AsyncRead(shared_from_this()), asio::detached); }
    void Write(std::vector<uint8_t> data) { asio::co_spawn(SocketStrand, AsyncWrite(std::move(data)), asio::detached); }
    void Close() { asio::co_spawn(SocketStrand, AsyncClose(), asio::detached); }

    constexpr uint64_t GetConnectionId() const { return ConnectionId; }
    constexpr const asio::ip::tcp::socket& GetSocket() const { return Socket; }

protected:
    asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpSocket> self)
    {
        BOOST_ASSERT(SocketStrand.running_in_this_thread());
        char buffer[4 * 1024];
        try
        {
            for (;;)
            {
                auto bytesTransferred = co_await Socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
                Write(std::vector<uint8_t>(buffer, buffer + bytesTransferred));
            }
        }
        catch (const std::exception& e)
        {
            Socket.close();
            std::cout << "exception: " << e.what() << std::endl;
        }
        BOOST_ASSERT(SocketStrand.running_in_this_thread());
        co_await asio::post(asio::bind_executor(TcpService->ServiceStrand, asio::use_awaitable));
        TcpService->Release(this);
    }

    asio::awaitable<void> AsyncWrite(std::vector<uint8_t> data)
    {
        try
        {
            BOOST_ASSERT(SocketStrand.running_in_this_thread());
            bool bIsWriteQueueEmpty = WriteQueue.empty();
            WriteQueue.push(std::move(data));
            if (!bIsWriteQueueEmpty)
                co_return;
            while (!WriteQueue.empty())
            {
                uint32_t bodySize = WriteQueue.front().size();
                bodySize = EndianCast(bodySize);
                auto bytesTransferred = co_await Socket.async_write_some(asio::buffer(&bodySize, sizeof(bodySize)), asio::use_awaitable);
                bytesTransferred = co_await Socket.async_write_some(asio::buffer(WriteQueue.front()), asio::use_awaitable);
                WriteQueue.pop();
            }
            BOOST_ASSERT(SocketStrand.running_in_this_thread());
        }
        catch (const std::exception& e)
        {
            Socket.close();
            std::cout << "exception: " << e.what() << std::endl;
        }
    }

    asio::awaitable<void> AsyncClose()
    {
        Socket.close();
        co_return;
    }

    ITcpService* TcpService;
    uint64_t ConnectionId;

    asio::ip::tcp::socket Socket;
    asio::strand<asio::io_context::executor_type> SocketStrand;
    std::queue<std::vector<uint8_t>> WriteQueue;

};

//struct FConnection {
//    std::shared_ptr<FTcpSocket> HardPtr;
//    std::weak_ptr<FTcpSocket> WeakPtr;
//};

struct FTcpConnection {
    asio::ip::tcp::endpoint RemoteEndpoint;
    std::shared_ptr<FTcpSocket> Socket;
    std::weak_ptr<FTcpSocket> SocketWeakPtr;
};

class FRpcService : public ITcpService
{
public:
    FRpcService(asio::io_context& ioContext)
        : ITcpService(ioContext)
    {}

    void Listen(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
    {
        auto acceptor = std::make_shared<asio::ip::tcp::acceptor>(ServiceStrand, asio::ip::tcp::endpoint(address, port));
        Acceptor = acceptor;
        asio::co_spawn(ServiceStrand, AsyncAccept(shared_from_this(), acceptor), asio::detached);
    }

    void Stop()
    {
        std::shared_ptr<asio::ip::tcp::acceptor> acceptor = Acceptor.lock();
        if (acceptor) {
            asio::post(ServiceStrand, [acceptor = std::move(acceptor)] { acceptor->close(); });
            while (!Acceptor.expired()) {
                std::this_thread::yield();
            }
        }
    }

    asio::awaitable<std::shared_ptr<FTcpSocket>> AsyncConnect(std::shared_ptr<FRpcService> self, std::string address = "127.0.0.1", asio::ip::port_type port = 7772)
    {
        std::shared_ptr<FTcpSocket> tcpSocket;
        try {
            asio::strand<asio::io_context::executor_type> SocketStrand = asio::make_strand(IoContext);

            asio::deadline_timer deadlineTimer(SocketStrand);

            asio::ip::tcp::socket socket(SocketStrand);
            deadlineTimer.expires_from_now(boost::posix_time::seconds(3));
            deadlineTimer.async_wait([&](boost::system::error_code errorCode) { if (!errorCode) socket.close(); });
            co_await socket.async_connect(asio::ip::tcp::endpoint(asio::ip::make_address(address), port), asio::use_awaitable);
            deadlineTimer.cancel();
            if (!socket.is_open()) {
                co_return nullptr;
            }

            std::cout << "4" << std::endl;
            if (tcpSocket = co_await AsyncInsertConnection(std::move(socket), SocketStrand))
            {
                tcpSocket->Read();
            }
        }
        catch (const std::exception& e) {
            std::cout << "exception: " << e.what() << std::endl;
        }
        co_return tcpSocket;
    }

    void Timeout(boost::system::error_code errorCode, asio::ip::tcp::socket& socket) {
        if (errorCode) {
            return;
        }
        if (socket.is_open()) {
            socket.close();
        }
    }


protected:

    bool Acquire(FTcpSocket* tcpSocket) override
    {
        BOOST_ASSERT(ServiceStrand.running_in_this_thread());
        auto insertResult = ConnectionMap.insert(std::pair(tcpSocket->GetConnectionId(), FTcpConnection{
            .RemoteEndpoint = tcpSocket->GetSocket().remote_endpoint(),
            .Socket = tcpSocket->shared_from_this(),
            .SocketWeakPtr = tcpSocket->weak_from_this(),
            }));
        return insertResult.second;
    }

    void Release(FTcpSocket* tcpSocket)  override
    {
        BOOST_ASSERT(ServiceStrand.running_in_this_thread());
        auto it = ConnectionMap.find(tcpSocket->GetConnectionId());
        if (it != ConnectionMap.end())
        {
            it->second.Socket.reset();
        }
    }

    asio::awaitable<std::shared_ptr<FTcpSocket>> AsyncInsertConnection(asio::ip::tcp::socket socket, asio::strand<asio::io_context::executor_type> SocketStrand)
    {
        auto remoteEndpoint = socket.remote_endpoint();
        uint64_t connectionId = remoteEndpoint.address().to_v4().to_uint();
        connectionId = (connectionId << 16) + remoteEndpoint.port();
        std::shared_ptr<FTcpSocket> tcpSocket = std::make_shared<FTcpSocket>(this, connectionId, std::move(socket), SocketStrand);
        if (!remoteEndpoint.address().is_v4())
        {
            co_return nullptr;
        }
        co_await asio::post(asio::bind_executor(ServiceStrand, asio::use_awaitable));
        BOOST_ASSERT(ServiceStrand.running_in_this_thread());
        if (!Acquire(tcpSocket.get()))
        {
            co_return nullptr;
        }
        std::string ip = remoteEndpoint.address().to_string();
        asio::ip::port_type port = remoteEndpoint.port();
        std::cout << "conn[" << ip << ":" << port << "]: connected" << std::endl;
        co_return tcpSocket;
    }

    asio::awaitable<void> AsyncRemoveConnection(uint64_t connectionId)
    {
        BOOST_ASSERT(ServiceStrand.running_in_this_thread());
        auto it = ConnectionMap.find(connectionId);
        if (it != ConnectionMap.end())
        {
            it->second.RemoteEndpoint;
            auto& endpoint = it->second.RemoteEndpoint;
            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: disconnected" << std::endl;
            ConnectionMap.erase(it);
        }
        co_return;
    }

    asio::awaitable<void> AsyncAccept(std::shared_ptr<ITcpService> self, std::shared_ptr<asio::ip::tcp::acceptor> acceptor) {
        try
        {
            for (;;)
            {
                asio::ip::tcp::endpoint remoteEndpoint;
                asio::strand<asio::io_context::executor_type> SocketStrand = asio::make_strand(IoContext);
                asio::ip::tcp::socket socket = co_await acceptor->async_accept(SocketStrand, remoteEndpoint, asio::use_awaitable);
                if (std::shared_ptr<FTcpSocket> tcpSocket = co_await AsyncInsertConnection(std::move(socket), SocketStrand))
                {
                    tcpSocket->Read();
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "exception: " << e.what() << std::endl;
        }

        BOOST_ASSERT(ServiceStrand.running_in_this_thread());
        for (auto& [id, connection] : ConnectionMap)
        {
            if (connection.Socket)
            {
                connection.Socket->Close();
                connection.Socket.reset();
            }
            else
            {
                if (auto Socket = connection.SocketWeakPtr.lock())
                {
                    Socket->Close();
                }
            }
        }
        asio::steady_timer timer(ServiceStrand);
        while (!ConnectionMap.empty())
        {
            timer.expires_after(std::chrono::milliseconds(1));
            co_await timer.async_wait(asio::use_awaitable);
        }
    }

protected:
    std::weak_ptr<asio::ip::tcp::acceptor> Acceptor;

    std::map<uint64_t, FTcpConnection> ConnectionMap;

    friend class FTcpSocket;
};


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

#include "../AsioRpc.h"

using namespace Cpp;

asio::awaitable<void> test(std::shared_ptr<FTcpContext> rpcService) {
    auto tcp = co_await rpcService->AsyncConnect(rpcService, "127.0.0.1", 7777);
    if (tcp)
    {
        std::cout << "tpc A";
        for (size_t i = 0; i < 256; i++)
        {
            auto str = std::to_string(i);
            tcp->Write(std::vector<uint8_t>{ str.begin(), str.end() });
        }
    }
    else
    {
        std::cout << "tpc B";
    }
}


int main(int argc, char* argv[]) {

    asio::io_context ioc;

    std::unique_ptr<asio::io_context::work> work = std::make_unique<asio::io_context::work>(ioc);
    std::thread t([&] {
        ioc.run();
    });
    {
        std::shared_ptr<FTcpContext> tcpContext = std::make_shared<FTcpContext>(ioc);
        tcpContext->Listen();
        asio::co_spawn(ioc, test(tcpContext), asio::detached);
        std::this_thread::sleep_for(std::chrono::seconds(64));
    }
    work.reset();
    t.join();
    return 0;
}