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
            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: connected" << std::endl;
            TcpContext->OnConnected(connection.get());
            try
            {
                char buffer[4 * 1024];
                for (;;)
                {
                    uint32_t bufferSize;
                    auto bytesTransferred = co_await asio::async_read(Socket, asio::buffer(&bufferSize, sizeof(bufferSize)), asio::use_awaitable);
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
