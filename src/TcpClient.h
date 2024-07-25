#pragma once
#include "TcpConnection.h"

namespace Cpp {

    class FTcpClient : public std::enable_shared_from_this<FTcpClient>
    {
    protected:
        struct FImpl : public ITcpContext {
        public:
            FImpl(asio::io_context& ioContext)
                : ITcpContext(ioContext)
                , Strand(asio::make_strand(ioContext))
            {}

            ~FImpl() {}

            virtual std::shared_ptr<FTcpConnection> NewConnection(asio::ip::address address, asio::ip::port_type port) {
                return std::make_shared<FTcpConnection>(shared_from_this(), Strand, asio::ip::tcp::endpoint(address, port));
            }

            asio::awaitable<void> AsyncConnect(std::shared_ptr<FImpl> self, std::shared_ptr<FTcpConnection> connection)
            {
                connection->CleanupFunc = [this, self, address = connection->RefEndpoint().address(), port = connection->RefEndpoint().port()] {
                    std::shared_ptr<FTcpConnection> connection = NewConnection(address, port);
                    asio::co_spawn(Strand, AsyncConnect(std::move(self), std::move(connection)), asio::detached);
                };
                WeakConnection = connection;
                BOOST_ASSERT(Strand.running_in_this_thread());
                try {
                    asio::steady_timer connectTimeoutTimer(connection->RefStrand());
                    connectTimeoutTimer.expires_from_now(std::chrono::milliseconds(static_cast<int64_t>(1000 * OperationTimeout)));
                    connectTimeoutTimer.async_wait([=](boost::system::error_code errorCode) { if (!errorCode) connection->RefSocket().close(); });
                    co_await connection->RefSocket().async_connect(connection->RefEndpoint(), asio::use_awaitable);
                    BOOST_ASSERT(Strand.running_in_this_thread() || connection->RefStrand().running_in_this_thread());
                    connectTimeoutTimer.cancel();
                    if (connection->RefSocket().is_open())
                    {
                        asio::co_spawn(connection->Strand, connection->AsyncRead(connection), asio::detached);
                    }
                }
                catch (const std::exception& e) {
                    Log(fmt::format("FTcpClient::AsyncConnect > exception : {}", e.what()).c_str());
                }
            }

            asio::strand<asio::io_context::executor_type> Strand;
            std::weak_ptr<FTcpConnection> WeakConnection;
        };
    public:
        FTcpClient(asio::io_context& ioContext, std::shared_ptr<FImpl> impl = nullptr)
            : Impl(impl ? std::move(impl) : std::make_shared<FImpl>(ioContext))
            , TcpContextInitializer([this] { this->InitTcpContext(); })
        {}

        ~FTcpClient() {
            Stop();
        }

        void Start(asio::ip::address address = asio::ip::address_v4::loopback(), asio::ip::port_type port = 7772)
        {
            Stop();
            if (TcpContextInitializer)
            {
                TcpContextInitializer();
                TcpContextInitializer = nullptr;
            }
            std::shared_ptr<FTcpConnection> connection = Impl->NewConnection(address, port);
            asio::co_spawn(Impl->Strand, Impl->AsyncConnect(Impl, std::move(connection)), asio::detached);
        }

        void Stop()
        {
            asio::post(Impl->Strand, [selfImpl = Impl] {
                BOOST_ASSERT(selfImpl->Strand.running_in_this_thread());
                if (std::shared_ptr<FTcpConnection> connection = selfImpl->WeakConnection.lock()) {
                    auto rawConnection = connection.get();
                    rawConnection->CleanupFunc = nullptr;
                    rawConnection->Close(std::move(connection));
                }
            });
        }

        ITcpContext* GetTcpContext() { return Impl.get(); };

    protected:

        virtual void InitTcpContext() {
        }

    protected:
        std::shared_ptr<FImpl> Impl;
        std::function<void()> TcpContextInitializer;
        bool bAutoReconnect = true;
    };

}