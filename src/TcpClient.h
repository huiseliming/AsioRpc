#pragma once
#include "TcpConnection.h"

namespace Cpp {

    class FTcpClient : public ITcpContext
    {
    public:

        FTcpClient(asio::io_context& ioContext)
            : ITcpContext(ioContext)
            , Strand(asio::make_strand(ioContext))
        {}

        ~FTcpClient() {
            Stop();
        }

        virtual std::shared_ptr<FTcpConnection> NewConnection(asio::ip::address address, asio::ip::port_type port) {
            return std::make_shared<FTcpConnection>(this, Strand, asio::ip::tcp::endpoint(address, port));
        }

        std::future<std::shared_ptr<FTcpConnection>> Start(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            return asio::co_spawn(Strand, AsyncConnect(address, port), asio::use_future);
        }

        void Stop()
        {
            asio::co_spawn(Strand, AsyncStop(ConnectionWeakPtr), asio::use_future).get();
        }

        asio::awaitable<void> AsyncStop(std::weak_ptr<FTcpConnection> connectionWeakPtr)
        {
            co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
            if (auto connection = connectionWeakPtr.lock()) {
                connection->Close();
            }
            asio::steady_timer timer(Strand);
            while (!connectionWeakPtr.expired()) {
                timer.expires_after(std::chrono::milliseconds(1));
                co_await timer.async_wait(asio::use_awaitable);
                BOOST_ASSERT(Strand.running_in_this_thread());
            }
        }

        asio::awaitable<std::shared_ptr<FTcpConnection>> AsyncConnect(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
            if (!ConnectionWeakPtr.expired())
            {
                co_return nullptr;
            }
            std::shared_ptr<FTcpConnection> connection = NewConnection(address, port);
            ConnectionWeakPtr = connection;
            try {
                asio::steady_timer connectTimeoutTimer(connection->RefStrand());
                connectTimeoutTimer.expires_from_now(std::chrono::milliseconds(static_cast<int64_t>(1000 * GetOperationTimeout())));
                connectTimeoutTimer.async_wait([=](boost::system::error_code errorCode) { if (!errorCode) connection->RefSocket().close(); });
                co_await connection->RefSocket().async_connect(connection->RefEndpoint(), asio::use_awaitable);
                connectTimeoutTimer.cancel();
                if (connection->RefSocket().is_open())
                {
                    asio::co_spawn(connection->Strand, [=]() -> asio::awaitable<void> {
                        BOOST_ASSERT(Strand.running_in_this_thread());
                        Connection = connection;
                        co_await connection->AsyncRead(connection);
                        BOOST_ASSERT(Strand.running_in_this_thread());
                        Connection.reset();
                    }, asio::detached);
                    co_return connection;
                }
            }
            catch (const std::exception& e) {
                std::cout << "exception: " << e.what() << std::endl;
            }
            co_return nullptr;
        }

    protected:
        asio::strand<asio::io_context::executor_type> Strand;
        std::shared_ptr<FTcpConnection> Connection;
        std::weak_ptr<FTcpConnection> ConnectionWeakPtr;

    };

}