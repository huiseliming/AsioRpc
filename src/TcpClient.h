#pragma once
#include "TcpContext.h"


namespace Private {

    class FTcpClient : public ITcpContext, public std::enable_shared_from_this<FTcpClient> {
    public:

        FTcpClient(asio::io_context& ioContext)
            : ITcpContext(ioContext)
            , Strand(asio::make_strand(ioContext))
        {}

        ~FTcpClient() {
            Stop();
        }

        std::future<std::shared_ptr<FTcpSocket>> Start(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            return asio::co_spawn(Strand, AsyncStart(address, port), asio::use_future);
        }

        asio::awaitable<std::shared_ptr<FTcpSocket>> AsyncStart(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            co_await AsyncStop(SocketWeakPtr);
            std::shared_ptr<FTcpSocket> tcpSocket = std::make_shared<FTcpSocket>(*this, asio::ip::tcp::endpoint(address, port));
            SocketWeakPtr = tcpSocket;
            co_return co_await AsyncConnect(std::move(tcpSocket));
        }

        void Stop()
        {
            asio::co_spawn(Strand, AsyncStop(SocketWeakPtr), asio::use_future).get();
        }

        asio::awaitable<void> AsyncStop(std::weak_ptr<FTcpSocket> tcpSocketWeakPtr)
        {
            if (!Strand.running_in_this_thread()) {
                co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
            }
            if (auto tcpSocket = tcpSocketWeakPtr.lock()) {
                tcpSocket->Close();
            }
            asio::steady_timer timer(Strand);
            while (!tcpSocketWeakPtr.expired()) {
                timer.expires_after(std::chrono::milliseconds(1));
                co_await timer.async_wait(asio::use_awaitable);
            }
        }

        asio::awaitable<std::shared_ptr<FTcpSocket>> AsyncConnect(std::shared_ptr<FTcpSocket> tcpSocket, asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            try {
                auto& socket = tcpSocket->GetSocketRef();
                auto& strand = tcpSocket->GetStrandRef();
                auto& endpoint = tcpSocket->GetEndpointRef();
                asio::deadline_timer deadlineTimer(strand);
                deadlineTimer.expires_from_now(boost::posix_time::seconds(3));
                deadlineTimer.async_wait([&](boost::system::error_code errorCode) { if (!errorCode) socket.close(); });
                co_await socket.async_connect(endpoint, asio::use_awaitable);
                deadlineTimer.cancel();
                if (socket.is_open() && AcquireSocket(tcpSocket.get())) 
                {
                    tcpSocket->Read();
                    co_return tcpSocket;
                }
            }
            catch (const std::exception& e) {
                std::cout << "exception: " << e.what() << std::endl;
            }
            co_return nullptr;
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
                    printf("client: ");
                    for (size_t i = 0; i < bytesTransferred; i++)
                    {
                        printf("%c", buffer[i]);
                    }
                    printf("\n");
                    BOOST_ASSERT(strand.running_in_this_thread());
                    //tcpSocket->Write(std::vector<uint8_t>(buffer, buffer + bytesTransferred));
                }
            }
            catch (const std::exception& e)
            {
                socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: disconnected" << std::endl;
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
            Socket = tcpSocket->shared_from_this();
            return true;
        }

        virtual void ReleaseSocket(FTcpSocket* tcpSocket) override
        {
            Socket.reset();
        }

    protected:
        asio::strand<asio::io_context::executor_type> Strand;
        std::shared_ptr<FTcpSocket> Socket;
        std::weak_ptr<FTcpSocket> SocketWeakPtr;

    };

}