#pragma once
#include "TcpConnection.h"

namespace Cpp {

    class FTcpServer 
    {
    protected:
        struct FImpl : public ITcpContext {
        public:
            FImpl(asio::io_context& ioContext)
                : ITcpContext(ioContext)
                , Strand(asio::make_strand(ioContext))
            {}

            ~FImpl() {}

            virtual std::shared_ptr<FTcpConnection> NewConnection() {
                return std::make_shared<FTcpConnection>(shared_from_this());
            }

            virtual asio::awaitable<void> AsyncAccept(std::shared_ptr<FImpl> self, std::shared_ptr<asio::ip::tcp::acceptor> acceptor) {
                std::map<FTcpConnection*, std::weak_ptr<FTcpConnection>> ConnectionMap;
                try
                {
                    for (;;)
                    {
                        std::shared_ptr<FTcpConnection> connection = NewConnection();
                        co_await acceptor->async_accept(connection->RefSocket(), connection->RefEndpoint(), asio::use_awaitable);
                        BOOST_ASSERT(Strand.running_in_this_thread());
                        if (!connection->RefEndpoint().address().is_v4())
                        {
                            continue;
                        }
                        co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
                        auto connectionRawPtr = connection.get();
                        auto insertResult = ConnectionMap.insert(std::make_pair(connectionRawPtr, connection));
                        if (insertResult.second)
                        {
                            connection->SetCleanupFunc([&, this] { asio::post(Strand, [&, this] {ConnectionMap.erase(connectionRawPtr); }); });
                            connection->Read();
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    Log(fmt::format("FTcpServer::AsyncAccept > exception : {}", e.what()).c_str());
                }
                BOOST_ASSERT(Strand.running_in_this_thread());
                for (auto& [id, weakConnection] : ConnectionMap)
                {
                    if (auto connection = weakConnection.lock())
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
            }

            asio::strand<asio::io_context::executor_type> Strand;
        };
    public:
        FTcpServer(asio::io_context& ioContext, std::shared_ptr<FImpl> impl = nullptr)
            : Impl(impl ? std::move(impl) : std::make_shared<FImpl>(ioContext))
            , Strand(asio::make_strand(Impl->IoContext))
        { }     

        FTcpServer(asio::io_context& ioContext, asio::strand<asio::io_context::executor_type> strand, std::shared_ptr<FImpl> impl = nullptr)
            : Impl(impl ? std::move(impl) : std::make_shared<FImpl>(ioContext))
            , Strand(strand)
        { }

        ~FTcpServer() {
            Stop();
        }

        void Start(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            Stop();
            auto acceptor = std::make_shared<asio::ip::tcp::acceptor>(Impl->Strand, asio::ip::tcp::endpoint(address, port));
            WeakAcceptor = acceptor;
            asio::co_spawn(Impl->Strand, Impl->AsyncAccept(Impl, acceptor), asio::detached);
        }

        void Stop()
        {
            if (std::shared_ptr<asio::ip::tcp::acceptor> acceptor = WeakAcceptor.lock()) {
                asio::post(Impl->Strand, [acceptor = std::move(acceptor)] { acceptor->close(); });
            }
        }

        //static void OnConnected(std::weak_ptr<FTcpServer> weakServer, FTcpConnection* connectionRawPtr) {
        //    if (auto server = weakServer.lock())
        //    {
        //        auto serverRawPtr = server.get();
        //        asio::post(serverRawPtr->Strand,
        //            [
        //                server = std::move(server), 
        //                connectionId = connectionRawPtr->GetId(), 
        //                connection = connectionRawPtr->shared_from_this()
        //            ]{ 
        //                server->ConnectionMap.insert(std::make_pair(connectionId, std::move(connection)));
        //        });
        //    }
        //}

        //static void OnDisconnected(std::weak_ptr<FTcpServer> weakServer, FTcpConnection* connectionRawPtr) {
        //    if (auto server = weakServer.lock())
        //    {
        //        auto serverRawPtr = server.get();
        //        asio::post(serverRawPtr->Strand,
        //            [
        //                server = std::move(server), 
        //                connectionId = connectionRawPtr->GetId(), 
        //                connection = connectionRawPtr->shared_from_this()
        //            ]{
        //                auto it = server->ConnectionMap.find(connectionId);
        //                if (it != server->ConnectionMap.end() && it->second == connection)
        //                {
        //                    server->ConnectionMap.erase(it);
        //                }
        //            });
        //    }
        //}

        ITcpContext* GetTcpContext() { return Impl.get(); };

    protected:
        std::shared_ptr<FImpl> Impl;
        std::weak_ptr<asio::ip::tcp::acceptor> WeakAcceptor;
        asio::strand<asio::io_context::executor_type> Strand;
        std::map<FConnectionId, std::shared_ptr<FTcpConnection>> ConnectionMap;

    };

}
