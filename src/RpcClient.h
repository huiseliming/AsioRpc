#pragma once
#include "TcpClient.h"
#include "RpcDispatcher.h"
#include "RpcConnection.h"

namespace Cpp 
{

    class FRpcClient : public FTcpClient 
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
            , RpcDispatcher(Impl)
        {
            //Impl->ConnectedFunc = [this](FTcpConnection* connection) {
            //    Connection = connection->shared_from_this();
            //    OnAttached();
            //};
            //Impl->DisconnectedFunc = [this](FTcpConnection* connection) {
            //    OnDetached();
            //    Connection.reset();
            //};
            //Impl->RecvDataFunc = [this](FTcpConnection* connection, const char* data, std::size_t size) {
            //    RpcDispatcher.RecvRpc(connection, data, size);
            //};
        }

        ~FRpcClient() {
        }

        template<typename Resp, typename ... Args>
        void Call(std::string func, Resp&& resp, Args&& ... args) {
            if (Connection)
            {
                RpcDispatcher.SendRpcRequest(Connection.get(), func, std::forward<Resp>(resp), std::forward<Args>(args)...);
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

        FRpcDispatcher RpcDispatcher;
        std::shared_ptr<FTcpConnection> Connection;
        std::function<void()> AttachedFunc;
        std::function<void()> DetachedFunc;

    };

}
