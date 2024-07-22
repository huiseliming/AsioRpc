#pragma once
#include "TcpConnection.h"

namespace Cpp {

    class FRpcConnection : public FTcpConnection {
    public:
        FRpcConnection(ITcpContext* tcpContext)
            : FTcpConnection(tcpContext)
        {}

        FRpcConnection(ITcpContext* tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
            : FTcpConnection(tcpContext, strand, endpoint)
        {}

        ~FRpcConnection() { }

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection) override
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            TcpContext->OnConnected(connection.get());
            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: connected" << std::endl;
            try
            {
                asio::steady_timer readTimeoutTimer(connection->RefStrand());
                for (;;)
                {
                    readTimeoutTimer.expires_from_now(std::chrono::milliseconds(static_cast<int64_t>(1000 * TcpContext->GetOperationTimeout())));
                    readTimeoutTimer.async_wait([=](boost::system::error_code errorCode) { if (!errorCode) connection->RefSocket().close(); });
                    uint32_t bufferSize;
                    auto bytesTransferred = co_await asio::async_read(Socket, asio::buffer(&bufferSize, sizeof(bufferSize)), asio::use_awaitable);
                    readTimeoutTimer.cancel();
                    std::vector<char> buffer;
                    buffer.resize(EndianCast(bufferSize));
                    bytesTransferred = co_await asio::async_read(Socket, asio::buffer(buffer), asio::use_awaitable);
                    TcpContext->OnRecvData(connection.get(), buffer.data(), buffer.size());
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

    };

}
