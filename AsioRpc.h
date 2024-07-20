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


namespace Cpp {

    class FTcpConnection;

    using namespace boost;
    using FConnectionId = std::pair<asio::ip::address_v4::uint_type, asio::ip::port_type>;

    template <typename T>
    inline T EndianCast(const T& val)
    {
        if constexpr (std::endian::native == std::endian::little) {
            return boost::endian::endian_reverse(val);
        }
        return val;
    }

    class ITcpContext {
    public:
        ITcpContext(asio::io_context& ioContext)
            : IoContext(ioContext)
            , Strand(asio::make_strand(ioContext))
        {}

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection) = 0;
        virtual asio::awaitable<void> AsyncWrite(std::shared_ptr<FTcpConnection> connection, std::vector<uint8_t> data) = 0;
        virtual asio::awaitable<void> AsyncAccept(std::shared_ptr<asio::ip::tcp::acceptor> acceptor) = 0;
        virtual bool AcquireConnection(FTcpConnection* connection) = 0;
        virtual void ReleaseConnection(FTcpConnection* connection) = 0;

        asio::io_context& GetIoContextRef() { return IoContext; }

    protected:
        asio::io_context& IoContext;
        asio::strand<asio::io_context::executor_type> Strand;

    };

    class FTcpConnection : public std::enable_shared_from_this<FTcpConnection> 
    {
    public:
        FTcpConnection(ITcpContext& tcpContext)
            : TcpContext(tcpContext)
            , Strand(asio::make_strand(tcpContext.GetIoContextRef()))
            , Socket(Strand)
        {}
        ~FTcpConnection() {
            TcpContext.ReleaseConnection(this);
        }

        void Read() { asio::co_spawn(Strand, TcpContext.AsyncRead(shared_from_this()), asio::detached); }
        void Write(std::vector<uint8_t> data) { asio::co_spawn(Strand, TcpContext.AsyncWrite(shared_from_this(), std::move(data)), asio::detached); }
        void Close() { asio::co_spawn(Strand, AsyncClose(shared_from_this()), asio::detached); }

        asio::ip::tcp::socket& GetSocketRef() { return Socket; }
        asio::strand<asio::io_context::executor_type>& GetStrandRef() { return Strand; }
        asio::ip::tcp::endpoint& GetEndpointRef() { return Endpoint; }
        FConnectionId GetId() { return std::make_pair(Endpoint.address().to_v4().to_uint(), Endpoint.port()); }
        std::queue<std::vector<uint8_t>>& GetWriteQueueRef() { return WriteQueue; }

    protected:
        asio::awaitable<void> AsyncClose(std::shared_ptr<FTcpConnection> self)
        {
            Socket.close();
            co_return;
        }

    protected:
        ITcpContext& TcpContext;
        asio::strand<asio::io_context::executor_type> Strand;
        asio::ip::tcp::socket Socket;
        asio::ip::tcp::endpoint Endpoint;
        std::queue<std::vector<uint8_t>> WriteQueue;
    };

    class FTcpContext: public ITcpContext {
    public:

        FTcpContext(asio::io_context& ioContext)
            : ITcpContext(ioContext)
        {}
        ~FTcpContext() {
            Stop();
        }
        void Listen(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            auto acceptor = std::make_shared<asio::ip::tcp::acceptor>(Strand, asio::ip::tcp::endpoint(address, port));
            Acceptor = acceptor;
            asio::co_spawn(Strand, AsyncAccept(acceptor), asio::detached);
        }

        void Stop()
        {
            if (std::shared_ptr<asio::ip::tcp::acceptor> acceptor = Acceptor.lock()) {
                asio::post(Strand, [acceptor = std::move(acceptor)] { acceptor->close(); });
            }
            while (!Acceptor.expired()) {
                std::this_thread::yield();
            }
            asio::co_spawn(Strand, [=]() -> asio::awaitable<void> {
                BOOST_ASSERT(Strand.running_in_this_thread());
                for (auto& [id, connection] : ConnectionMap)
                {
                    if (auto Socket = connection.lock())
                    {
                        Socket->Close();
                    }
                }
                asio::steady_timer timer(Strand);
                while (!ConnectionMap.empty())
                {
                    timer.expires_after(std::chrono::milliseconds(1));
                    co_await timer.async_wait(asio::use_awaitable);
                }
            }, asio::use_future).get();
        }

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection) override
        {
            asio::strand<asio::io_context::executor_type>& strand = connection->GetStrandRef();
            asio::ip::tcp::socket& socket = connection->GetSocketRef();
            BOOST_ASSERT(strand.running_in_this_thread());
            char buffer[4 * 1024];
            try
            {
                for (;;)
                {
                    auto bytesTransferred = co_await socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
                    connection->Write(std::vector<uint8_t>(buffer, buffer + bytesTransferred));
                }
            }
            catch (const std::exception& e)
            {
                socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
            BOOST_ASSERT(strand.running_in_this_thread());
        }

        virtual asio::awaitable<void> AsyncWrite(std::shared_ptr<FTcpConnection> connection, std::vector<uint8_t> data) override
        {
            asio::strand<asio::io_context::executor_type>& strand = connection->GetStrandRef();
            asio::ip::tcp::socket& socket = connection->GetSocketRef();
            std::queue<std::vector<uint8_t>>& writeQueue = connection->GetWriteQueueRef();
            try
            {
                BOOST_ASSERT(strand.running_in_this_thread());
                bool bIsWriteQueueEmpty = writeQueue.empty();
                writeQueue.push(std::move(data));
                if (!bIsWriteQueueEmpty)
                    co_return;
                while (!writeQueue.empty())
                {
                    auto bytesTransferred = co_await socket.async_write_some(asio::buffer(writeQueue.front()), asio::use_awaitable);
                    writeQueue.pop();
                }
                BOOST_ASSERT(strand.running_in_this_thread());
            }
            catch (const std::exception& e)
            {
                socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
        }

    protected:
        virtual asio::awaitable<void> AsyncAccept(std::shared_ptr<asio::ip::tcp::acceptor> acceptor) override {
            try
            {
                for (;;)
                {
                    std::shared_ptr<FTcpConnection> newConnection = std::make_shared<FTcpConnection>(*this);
                    auto& socket = newConnection->GetSocketRef();
                    auto& endpoint = newConnection->GetEndpointRef();
                    asio::strand<asio::io_context::executor_type> SocketStrand = asio::make_strand(IoContext);
                    co_await acceptor->async_accept(socket, endpoint, asio::use_awaitable);
                    if (!endpoint.address().is_v4())
                    {
                        continue;
                    }
                    co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
                    if (AcquireConnection(newConnection.get()))
                    {
                        newConnection->Read();
                    }
                }
            }
            catch (const std::exception& e)
            {
                std::cout << "exception: " << e.what() << std::endl;
            }
        }

        virtual bool AcquireConnection(FTcpConnection* connection) override
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            auto& endpoint = connection->GetEndpointRef();
            auto insertResult = ConnectionMap.insert(std::make_pair(std::make_pair(endpoint.address().to_v4().to_uint(), endpoint.port()), connection->shared_from_this()));
            if (!insertResult.second)
            {
                return false;
            }
            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: connected" << std::endl;
            return true;
        }

        virtual void ReleaseConnection(FTcpConnection* connection) override
        {
            asio::co_spawn(Strand, [this](FConnectionId connectionId) -> asio::awaitable<void> {
                BOOST_ASSERT(Strand.running_in_this_thread());
                auto it = ConnectionMap.find(connectionId);
                if (it != ConnectionMap.end())
                {
                    std::cout << "conn[" << asio::ip::address_v4(connectionId.first).to_string() << ":" << connectionId.second << "]: disconnected" << std::endl;
                    ConnectionMap.erase(it);
                }
                co_return;
            } (connection->GetId()), asio::detached);
        }

    protected:
        std::weak_ptr<asio::ip::tcp::acceptor> Acceptor;
        std::map<FConnectionId, std::weak_ptr<FTcpConnection>> ConnectionMap;

    };


}