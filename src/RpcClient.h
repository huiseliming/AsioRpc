#pragma once
#include "TcpServer.h"

namespace Private {

	class FRpcSocket : public FTcpSocket {
    public:
        FRpcSocket(ITcpContext& tcpContext)
            : FTcpSocket(tcpContext)
        {}

        FRpcSocket(ITcpContext& tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
            : FTcpSocket(tcpContext, strand, endpoint)
        {}

        ~FRpcSocket() { }

        void AsyncCall() {
       
        }

	};

    class FRpcServer : public FTcpServer {
    public:
        FRpcServer(asio::io_context& ioContext)
            : FTcpServer(ioContext)
        {}

        ~FRpcServer() { }

        virtual asio::awaitable<void> AsyncAccept(std::shared_ptr<asio::ip::tcp::acceptor> acceptor) override {
            try
            {
                for (;;)
                {
                    std::shared_ptr<FTcpSocket> newSocket = std::make_shared<FRpcSocket>(*this);
                    auto& socket = newSocket->GetSocketRef();
                    auto& endpoint = newSocket->GetEndpointRef();
                    asio::strand<asio::io_context::executor_type> SocketStrand = asio::make_strand(IoContext);
                    co_await acceptor->async_accept(socket, endpoint, asio::use_awaitable);
                    if (!endpoint.address().is_v4())
                    {
                        continue;
                    }
                    co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
                    if (AcquireSocket(newSocket.get()))
                    {
                        newSocket->Read();
                    }
                }
            }
            catch (const std::exception& e)
            {
                std::cout << "exception: " << e.what() << std::endl;
            }
        }
    };

}



















