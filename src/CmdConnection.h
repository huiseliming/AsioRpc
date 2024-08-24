#pragma once
#include "TcpConnection.h"

namespace Cpp {

    class FCmdConnection : public FTcpConnection {
    public:
        FCmdConnection(std::shared_ptr<ITcpContext> tcpContext)
            : FTcpConnection(tcpContext)
            , WaitCmdResponseOrTimeoutTimer(Strand)
        {}

        FCmdConnection(std::shared_ptr<ITcpContext> tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
            : FTcpConnection(tcpContext, strand, endpoint)
            , WaitCmdResponseOrTimeoutTimer(Strand)
        {}

        ~FCmdConnection() { }

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection)
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            TcpContext->OnConnected(connection.get());
            TcpContext->Log(fmt::format("FCmdConnection::AsyncRead[{}:{}] > connected", Endpoint.address().to_string(), Endpoint.port()).c_str());

            asio::steady_timer heartbeatTimeoutTimer(Strand);
            auto heartbeatSenderFuture = asio::co_spawn(Strand, AsyncSendHeartbeat(heartbeatTimeoutTimer), asio::use_future);
            asio::steady_timer timer(Strand);
            try
            {
                char buffer[4 * 1024];
                for (;;)
                {
                    timer.expires_from_now(std::chrono::milliseconds(static_cast<int64_t>(1000 * OperationTimeout)));
                    timer.async_wait([this, connection](boost::system::error_code errorCode) { if (!errorCode) Socket.close(); });
                    auto bytesTransferred = co_await Socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
                    timer.cancel();
                    BOOST_ASSERT(Strand.running_in_this_thread());
                    WaitCmdResponseOrTimeoutTimer.cancel();
                    TcpContext->OnRecvData(connection.get(), std::vector<uint8_t>(buffer, buffer + bytesTransferred));
                }
            }
            catch (const std::exception& e)
            {
                Socket.close();
                TcpContext->Log(fmt::format("FCmdConnection::AsyncRead[{}:{}] > exception : {}", Endpoint.address().to_string(), Endpoint.port(), e.what()).c_str());
            }
            timer.cancel();
            heartbeatTimeoutTimer.cancel();
            WaitCmdResponseOrTimeoutTimer.cancel();
            while (std::future_status::ready != heartbeatSenderFuture.wait_for(std::chrono::milliseconds(0)))
            {
                timer.expires_after(std::chrono::milliseconds(1));
                co_await timer.async_wait(asio::use_awaitable);
            }
            heartbeatSenderFuture.get();
            TcpContext->Log(fmt::format("FCmdConnection::AsyncRead[{}:{}] > disconnected", Endpoint.address().to_string(), Endpoint.port()).c_str());
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
                    if (!WriteQueue.front().empty())
                    {
                        auto bytesTransferred = co_await Socket.async_write_some(asio::buffer(WriteQueue.front()), asio::use_awaitable);
                        BOOST_ASSERT(Strand.running_in_this_thread());
                        boost::system::error_code errorCode;
                        WaitCmdResponseOrTimeoutTimer.expires_from_now(std::chrono::milliseconds(std::max(static_cast<int64_t>(1LL), static_cast<int64_t>(OperationTimeout * 1000 / 4) - 1)));
                        std::tie(errorCode) = co_await WaitCmdResponseOrTimeoutTimer.async_wait(asio::as_tuple(asio::use_awaitable));
                    }
                    WriteQueue.pop();
                }
            }
            catch (const std::exception& e)
            {
                Socket.close();
                TcpContext->Log(fmt::format("FCmdConnection::AsyncWrite[{}:{}] > exception : {}", Endpoint.address().to_string(), Endpoint.port(), e.what()).c_str());
            }
        }

    private:
        asio::steady_timer WaitCmdResponseOrTimeoutTimer;
    };

}