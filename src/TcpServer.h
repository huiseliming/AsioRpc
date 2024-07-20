#pragma once
#include "TcpContext.h"


namespace Private {

    class FTcpServer : public ITcpContext, public std::enable_shared_from_this<FTcpServer> {
    public:
        FTcpServer(asio::io_context& ioContext)
            : ITcpContext(ioContext)
        {}

        ~FTcpServer() {
            Stop();
        }

        void Start(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            Stop();
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
            Acceptor.reset();
            asio::co_spawn(Strand, [=]() -> asio::awaitable<void> {
                for (auto& [id, tcpSocket] : SocketMap)
                {
                    if (auto Socket = tcpSocket.lock())
                    {
                        Socket->Close();
                    }
                }
                asio::steady_timer timer(Strand);
                while (!SocketMap.empty())
                {
                    timer.expires_after(std::chrono::milliseconds(1));
                    co_await timer.async_wait(asio::use_awaitable);
                    BOOST_ASSERT(Strand.running_in_this_thread());
                }
                std::cout << "sss" << std::endl;
            }, asio::use_future).get();
            std::cout << "bbb" << std::endl;
        }

        asio::awaitable<void> AsyncAccept(std::shared_ptr<asio::ip::tcp::acceptor> acceptor) {
            try
            {
                for (;;)
                {
                    std::shared_ptr<FTcpSocket> newSocket = std::make_shared<FTcpSocket>(*this);
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

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpSocket> tcpSocket) override
        {
            asio::strand<asio::io_context::executor_type>& strand = tcpSocket->GetStrandRef();
            asio::ip::tcp::socket& socket = tcpSocket->GetSocketRef();
            BOOST_ASSERT(strand.running_in_this_thread());
            char buffer[4 * 1024];
            auto& endpoint = tcpSocket->GetEndpointRef();
            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: connected" << std::endl;
            try
            {
                for (;;)
                {
                    auto bytesTransferred = co_await socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
                    tcpSocket->Write(std::vector<uint8_t>(buffer, buffer + bytesTransferred));
                }
            }
            catch (const std::exception& e)
            {
                socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: disconnected" << std::endl;
            BOOST_ASSERT(strand.running_in_this_thread());
        }

        virtual asio::awaitable<void> AsyncWrite(std::shared_ptr<FTcpSocket> tcpSocket, std::vector<uint8_t> data) override
        {
            asio::strand<asio::io_context::executor_type>& strand = tcpSocket->GetStrandRef();
            asio::ip::tcp::socket& socket = tcpSocket->GetSocketRef();
            std::queue<std::vector<uint8_t>>& writeQueue = tcpSocket->GetWriteQueueRef();
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
        virtual bool AcquireSocket(FTcpSocket* tcpSocket) override
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            auto& endpoint = tcpSocket->GetEndpointRef();
            auto insertResult = SocketMap.insert(std::make_pair(std::make_pair(endpoint.address().to_v4().to_uint(), endpoint.port()), tcpSocket->shared_from_this()));
            if (!insertResult.second)
            {
                return false;
            }
            return true;
        }

        virtual void ReleaseSocket(FTcpSocket* tcpSocket) override
        {
            asio::co_spawn(Strand, [this](FSocketId tcpSocketId) -> asio::awaitable<void> {
                BOOST_ASSERT(Strand.running_in_this_thread());
                auto it = SocketMap.find(tcpSocketId);
                if (it != SocketMap.end())
                {
                    SocketMap.erase(it);
                }
                co_return;
            } (tcpSocket->GetId()), asio::detached);
        }

    protected:
        std::weak_ptr<asio::ip::tcp::acceptor> Acceptor;
        std::map<FSocketId, std::weak_ptr<FTcpSocket>> SocketMap;

    };

}