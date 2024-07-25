#pragma once
#include "TcpConnection.h"

namespace Cpp {

    class FRpcConnection : public FTcpConnection {
    public:
        FRpcConnection(std::shared_ptr<ITcpContext> tcpContext)
            : FTcpConnection(tcpContext)
        {}

        FRpcConnection(std::shared_ptr<ITcpContext> tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
            : FTcpConnection(tcpContext, strand, endpoint)
        {}

        ~FRpcConnection() { }

        asio::awaitable<void> AsyncSendHeartbeat(asio::steady_timer& heartbeatTimeoutTimer) {
            while (Socket.is_open())
            {
                Write({ 0x00, 0x00, 0x00, 0x00 });
                heartbeatTimeoutTimer.expires_from_now(std::chrono::milliseconds(std::max(1LL, static_cast<int64_t>(1000 * TcpContext->OperationTimeout) / 2 - 1)));
                system::error_code errorCode;
                std::tie(errorCode) = co_await heartbeatTimeoutTimer.async_wait(asio::as_tuple(asio::use_awaitable));
                if (errorCode) break;
            }
        }

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection) override
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            TcpContext->OnConnected(connection.get());
            TcpContext->Log(fmt::format("FRpcConnection::AsyncRead[{}:{}] > connected", Endpoint.address().to_string(), Endpoint.port()).c_str());

            asio::steady_timer heartbeatTimeoutTimer(Strand);
            auto heartbeatSenderFuture= asio::co_spawn(Strand, AsyncSendHeartbeat(heartbeatTimeoutTimer), asio::use_future);
            asio::steady_timer timer(Strand);
            try
            {
                for (;;)
                {
                    timer.expires_from_now(std::chrono::milliseconds(static_cast<int64_t>(1000 * TcpContext->OperationTimeout)));
                    timer.async_wait([this, connection](boost::system::error_code errorCode) { if (!errorCode) Socket.close(); });
                    uint32_t bufferSize;
                    auto bytesTransferred = co_await asio::async_read(Socket, asio::buffer(&bufferSize, sizeof(bufferSize)), asio::use_awaitable);
                    timer.cancel();

                    if (bufferSize == 0) continue;

                    std::vector<char> buffer;
                    buffer.resize(EndianCast(bufferSize));
                    timer.expires_from_now(std::chrono::milliseconds(static_cast<int64_t>(1000 * TcpContext->OperationTimeout)));
                    timer.async_wait([this, connection](boost::system::error_code errorCode) { if (!errorCode) Socket.close(); });
                    bytesTransferred = co_await asio::async_read(Socket, asio::buffer(buffer), asio::use_awaitable);
                    timer.cancel();
                    TcpContext->OnRecvData(connection.get(), buffer.data(), buffer.size());
                }
            }
            catch (const std::exception& e)
            {
                Socket.close();
                TcpContext->Log(fmt::format("FRpcConnection::AsyncRead[{}:{}] > exception : {}", Endpoint.address().to_string(), Endpoint.port(), e.what()).c_str());
            }
            timer.cancel();
            heartbeatTimeoutTimer.cancel();
            while (!heartbeatSenderFuture._Is_ready())
            {
                timer.expires_after(std::chrono::milliseconds(1));
                co_await timer.async_wait(asio::use_awaitable);
            }
            heartbeatSenderFuture.get();
            TcpContext->Log(fmt::format("FRpcConnection::AsyncRead[{}:{}] > disconnected", Endpoint.address().to_string(), Endpoint.port()).c_str());
            TcpContext->OnDisconnected(connection.get());
        }

    };

}
