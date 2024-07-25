#pragma once
#include "TcpClient.h"
#include "RpcDispatcher.h"
#include "RpcConnection.h"

namespace Cpp 
{

    class FRpcClient : public FTcpClient, public std::enable_shared_from_this<FRpcClient>
    {
    protected:
        struct FImpl : public FTcpClient::FImpl {
        public:
            FImpl(asio::io_context& ioContext)
                : FTcpClient::FImpl(ioContext)
            {}
            virtual std::shared_ptr<FTcpConnection> NewConnection(asio::ip::address address, asio::ip::port_type port) override {
                return std::make_shared<FRpcConnection>(shared_from_this(), Strand, asio::ip::tcp::endpoint(address, port));
            }
        };
    public:
        FRpcClient(asio::io_context& ioContext)
            : FTcpClient(ioContext, std::make_shared<FImpl>(ioContext))
            , RpcDispatcher(std::make_shared<FRpcDispatcher>(Impl))
            , Strand(asio::make_strand(ioContext))
        {

        }

        ~FRpcClient() {
        }

        void OnConnected(std::shared_ptr<FTcpConnection> connection) {
            BOOST_ASSERT(Strand.running_in_this_thread());
            Connection = connection->shared_from_this();
            OnAttached();
        }

        void OnDisconnected(std::shared_ptr<FTcpConnection> connection) {
            BOOST_ASSERT(Strand.running_in_this_thread());
            if (Connection == connection)
            {
                Connection.reset();
            }
            OnDetached();
        }

        virtual void InitTcpContext() override {
            Impl->ConnectedFunc = [this, weakSelf = weak_from_this()](FTcpConnection* rawConnection) {
                if (auto self = weakSelf.lock())
                {
                    asio::post(Strand, [this, self = std::move(self), connection = rawConnection->shared_from_this()] { OnConnected(std::move(connection)); });
                }
            };
            Impl->DisconnectedFunc = [this, weakSelf = weak_from_this()](FTcpConnection* rawConnection) {
                if (auto self = weakSelf.lock())
                {
                    asio::post(Strand, [this, self = std::move(self), connection = rawConnection->shared_from_this()] { OnDisconnected(std::move(connection)); });
                }
            };
            Impl->RecvDataFunc = [rpcDispatcher = RpcDispatcher](FTcpConnection* connection, const char* data, std::size_t size) {
                rpcDispatcher->RecvRpc(connection, data, size);
            };
        }

        template<typename Func, typename ... Args>
        void Call(std::string name, Func&& func, Args&& ... args) {
            if (Connection)
            {
                asio::co_spawn(Impl->IoContext, RpcDispatcher->AsyncCall(RpcDispatcher, Connection, std::move(name), FRpcDispatcher::ToRequestFunc(func), std::make_tuple(std::forward<Args>(args)...)), asio::detached);
            }
        }

        void OnAttached() {
            if (AttachedFunc) AttachedFunc();
        }

        void OnDetached() {
            if (DetachedFunc) DetachedFunc();
        }

        void SetAttachedFunc(std::function<void()> func) {
            AttachedFunc = func;
        }

        void SetDetachedFunc(std::function<void()> func) {
            DetachedFunc = func;
        }

        FRpcDispatcher& RefRpcDispatcher() { return *RpcDispatcher; }

    protected:
        std::shared_ptr<FRpcDispatcher> RpcDispatcher;
        asio::strand<asio::io_context::executor_type> Strand;
        std::shared_ptr<FTcpConnection> Connection;
        std::function<void()> AttachedFunc;
        std::function<void()> DetachedFunc;

    };

}
