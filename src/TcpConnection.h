#pragma once
#include "TcpContext.h"

namespace Cpp {

    using FConnectionId = std::pair<asio::ip::address_v4::uint_type, asio::ip::port_type>;

    class FTcpConnection : public std::enable_shared_from_this<FTcpConnection>
    {
    public:
        FTcpConnection(ITcpContext* tcpContext)
            : TcpContext(tcpContext)
            , Strand(asio::make_strand(tcpContext->RefIoContext()))
            , Socket(Strand)
        {}

        FTcpConnection(ITcpContext* tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
            : TcpContext(tcpContext)
            , Strand(strand)
            , Socket(strand)
            , Endpoint(endpoint)
        {}

        virtual ~FTcpConnection() {
            if (CleanupFunc)
            {
                CleanupFunc();
                CleanupFunc = nullptr;
            }
        }

        FConnectionId GetId() { return std::make_pair(Endpoint.address().to_v4().to_uint(), Endpoint.port()); }

        void Read() { asio::co_spawn(Strand, AsyncRead(shared_from_this()), asio::detached); }
        void Write(std::vector<uint8_t> data) { asio::co_spawn(Strand, AsyncWrite(shared_from_this(), std::move(data)), asio::detached); }
        void Close() { asio::co_spawn(Strand, AsyncClose(shared_from_this()), asio::detached); }

        void Read(std::shared_ptr<FTcpConnection> connection) { asio::co_spawn(Strand, AsyncRead(std::move(connection)), asio::detached); }
        void Write(std::shared_ptr<FTcpConnection> connection, std::vector<uint8_t> data) { asio::co_spawn(Strand, AsyncWrite(std::move(connection), std::move(data)), asio::detached); }
        void Close(std::shared_ptr<FTcpConnection> connection) { asio::co_spawn(Strand, AsyncClose(std::move(connection)), asio::detached); }

        asio::ip::tcp::socket& RefSocket() { return Socket; }
        asio::strand<asio::io_context::executor_type>& RefStrand() { return Strand; }
        asio::ip::tcp::endpoint& RefEndpoint() { return Endpoint; }
        std::queue<std::vector<uint8_t>>& RefWriteQueue() { return WriteQueue; }

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection)
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            TcpContext->OnConnected(connection.get());
            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: connected" << std::endl;
            try
            {
                asio::steady_timer readTimeoutTimer(connection->RefStrand());
                char buffer[4 * 1024];
                for (;;)
                {
                    readTimeoutTimer.expires_from_now(std::chrono::milliseconds(static_cast<int64_t>(1000 * TcpContext->GetOperationTimeout())));
                    readTimeoutTimer.async_wait([=](boost::system::error_code errorCode) { if (!errorCode) connection->RefSocket().close(); });
                    auto bytesTransferred = co_await Socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
                    readTimeoutTimer.cancel();
                    BOOST_ASSERT(Strand.running_in_this_thread());
                    TcpContext->OnRecvData(connection.get(), buffer, bytesTransferred);
                }
            }
            catch (const std::exception& e)
            {
                Socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: disconnected" << std::endl;
            TcpContext->OnDisconnected(connection.get());
        }

        virtual asio::awaitable<void> AsyncWrite(std::shared_ptr<FTcpConnection> connection, std::vector<uint8_t> data)
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            try
            {
                bool bIsWriteQueueEmpty = WriteQueue.empty();
                WriteQueue.push(std::move(data));
                if (!bIsWriteQueueEmpty)
                    co_return;
                while (!WriteQueue.empty())
                {
                    auto bytesTransferred = co_await Socket.async_write_some(asio::buffer(WriteQueue.front()), asio::use_awaitable);
                    BOOST_ASSERT(Strand.running_in_this_thread());
                    WriteQueue.pop();
                }
            }
            catch (const std::exception& e)
            {
                Socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
        }

        asio::awaitable<void> AsyncClose(std::shared_ptr<FTcpConnection> self)
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            co_return Socket.close();
        }

    protected:
        template<typename Func>
        void SetCleanupFunc(Func&& func) {
            CleanupFunc = std::forward<Func>(func);
        }

    protected:
        ITcpContext* TcpContext;
        asio::strand<asio::io_context::executor_type> Strand;
        asio::ip::tcp::socket Socket;
        asio::ip::tcp::endpoint Endpoint;
        std::queue<std::vector<uint8_t>> WriteQueue;
        std::function<void()> CleanupFunc;

        friend class FTcpServer;
        friend class FTcpClient;
    };

}