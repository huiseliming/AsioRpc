#pragma once
#include "TcpConnection.h"

namespace Cpp {

    class FTcpServer : public ITcpContext {
    public:
        FTcpServer(asio::io_context& ioContext)
            : ITcpContext(ioContext)
            , Strand(asio::make_strand(ioContext))
        {}

        ~FTcpServer() {
            Stop();
        }

        virtual std::shared_ptr<FTcpConnection> NewConnection() {
            return std::make_shared<FTcpConnection>(this);
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
                asio::dispatch(Strand, [acceptor = std::move(acceptor)] { acceptor->close(); });
            }
            while (!Acceptor.expired()) {
                std::this_thread::yield();
            }
            Acceptor.reset();
            asio::co_spawn(Strand, [=]() -> asio::awaitable<void> {
                BOOST_ASSERT(Strand.running_in_this_thread());
                for (auto& [id, connectionWeakPtr] : ConnectionMap)
                {
                    if (auto connection = connectionWeakPtr.lock())
                    {
                        connection->Close();
                    }
                }
                asio::steady_timer timer(Strand);
                while (!ConnectionMap.empty())
                {
                    timer.expires_after(std::chrono::milliseconds(1));
                    co_await timer.async_wait(asio::use_awaitable);
                    BOOST_ASSERT(Strand.running_in_this_thread());
                }
                }, asio::use_future).get();
        }

        virtual asio::awaitable<void> AsyncAccept(std::shared_ptr<asio::ip::tcp::acceptor> acceptor) {
            try
            {
                for (;;)
                {
                    std::shared_ptr<FTcpConnection> connection = NewConnection();
                    co_await acceptor->async_accept(connection->RefSocket(), connection->RefEndpoint(), asio::use_awaitable);
                    if (!connection->RefEndpoint().address().is_v4())
                    {
                        continue;
                    }
                    co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
                    auto connectionId = connection->GetId();
                    auto insertResult = ConnectionMap.insert(std::make_pair(connectionId, connection));
                    if (insertResult.second)
                    {
                        connection->SetCleanupFunc([=, this] {
                            asio::co_spawn(Strand, [=, this]() -> asio::awaitable<void> {
                                BOOST_ASSERT(Strand.running_in_this_thread());
                                ConnectionMap.erase(connectionId);
                                co_return;
                                }, asio::detached);
                            });
                        connection->Read();
                    }
                }
            }
            catch (const std::exception& e)
            {
                std::cout << "exception: " << e.what() << std::endl;
            }
        }

    protected:
        std::weak_ptr<asio::ip::tcp::acceptor> Acceptor;
        asio::strand<asio::io_context::executor_type> Strand;
        std::map<FConnectionId, std::weak_ptr<FTcpConnection>> ConnectionMap;
    };

}