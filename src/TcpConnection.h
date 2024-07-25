#pragma once
#include "TcpContext.h"

namespace Cpp {

    using FConnectionId = std::pair<asio::ip::address_v4::uint_type, asio::ip::port_type>;

    class FTcpConnection : public std::enable_shared_from_this<FTcpConnection>
    {
    public:
        FTcpConnection(std::shared_ptr<ITcpContext> tcpContext)
            : TcpContext(std::move(tcpContext))
            , Strand(asio::make_strand(TcpContext->IoContext))
            , Socket(Strand)
        {}

        FTcpConnection(std::shared_ptr<ITcpContext> tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
            : TcpContext(std::move(tcpContext))
            , Strand(strand)
            , Socket(strand)
            , Endpoint(endpoint)
        {}

        virtual ~FTcpConnection() {
            if (PreDtorFunc)
            {
                PreDtorFunc();
                PreDtorFunc = nullptr;
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

        asio::awaitable<void> AsyncSendHeartbeat(asio::steady_timer& heartbeatTimeoutTimer) {
            while (Socket.is_open())
            {
                Write(TcpContext->HeartbeatData);
                heartbeatTimeoutTimer.expires_from_now(std::chrono::milliseconds(std::max(1LL, static_cast<int64_t>(1000 * TcpContext->OperationTimeout) / 2 - 1)));
                system::error_code errorCode;
                std::tie(errorCode) = co_await heartbeatTimeoutTimer.async_wait(asio::as_tuple(asio::use_awaitable));
                if (errorCode) break;
            }
        }

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection)
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            TcpContext->OnConnected(connection.get());
            TcpContext->Log(fmt::format("FTcpConnection::AsyncRead[{}:{}] > connected", Endpoint.address().to_string(), Endpoint.port()).c_str());

            asio::steady_timer heartbeatTimeoutTimer(Strand);
            auto heartbeatSenderFuture = asio::co_spawn(Strand, AsyncSendHeartbeat(heartbeatTimeoutTimer), asio::use_future);
            asio::steady_timer timer(Strand);
            try
            {
                char buffer[4 * 1024];
                for (;;)
                {
                    timer.expires_from_now(std::chrono::milliseconds(static_cast<int64_t>(1000 * TcpContext->OperationTimeout)));
                    timer.async_wait([this, connection](boost::system::error_code errorCode) { if (!errorCode) Socket.close(); });
                    auto bytesTransferred = co_await Socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
                    timer.cancel();
                    BOOST_ASSERT(Strand.running_in_this_thread());
                    TcpContext->OnRecvData(connection.get(), buffer, bytesTransferred);
                }
            }
            catch (const std::exception& e)
            {
                Socket.close();
                TcpContext->Log(fmt::format("FTcpConnection::AsyncRead[{}:{}] > exception : {}", Endpoint.address().to_string(), Endpoint.port(), e.what()).c_str());
            }
            timer.cancel();
            heartbeatTimeoutTimer.cancel();
            while (!heartbeatSenderFuture._Is_ready())
            {
                timer.expires_after(std::chrono::milliseconds(1));
                co_await timer.async_wait(asio::use_awaitable);
            }
            heartbeatSenderFuture.get();
            TcpContext->Log(fmt::format("FTcpConnection::AsyncRead[{}:{}] > disconnected", Endpoint.address().to_string(), Endpoint.port()).c_str());
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
                TcpContext->Log(fmt::format("FTcpConnection::AsyncWrite[{}:{}] > exception : {}", Endpoint.address().to_string(), Endpoint.port(), e.what()).c_str());
            }
        }

        asio::awaitable<void> AsyncClose(std::shared_ptr<FTcpConnection> self)
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            co_return Socket.close();
        }

    protected:
        std::shared_ptr<ITcpContext> TcpContext;
        asio::strand<asio::io_context::executor_type> Strand;
        asio::ip::tcp::socket Socket;
        asio::ip::tcp::endpoint Endpoint;
        std::queue<std::vector<uint8_t>> WriteQueue;
        std::function<void()> PreDtorFunc;

        friend class FTcpClient;
        friend class FTcpServer;
    };

}