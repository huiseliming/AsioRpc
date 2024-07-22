#pragma once
#include "TcpContext.h"
#include "RpcDispatcher.h"
#include "RpcConnection.h"

namespace Cpp 
{

    class FRpcClient : public FTcpClient 
    {
    public:
        FRpcClient(asio::io_context& ioContext)
            : FTcpClient(ioContext)
            , RpcDispatcher(this)
        {}

        ~FRpcClient() {
            Stop();
        }

        virtual std::shared_ptr<FTcpConnection> NewConnection(asio::ip::address address, asio::ip::port_type port) {
            return std::make_shared<FRpcConnection>(this, Strand, asio::ip::tcp::endpoint(address, port));
        }

        virtual void OnConnected(FTcpConnection* connection) override {
            if (OnConnectedFunc) OnConnectedFunc(connection);
        }

        virtual void OnRecvData(FTcpConnection* connection, const char* data, std::size_t size) override {
            RpcDispatcher.RecvRpc(connection, data, size);
        }

        virtual void OnDisconnected(FTcpConnection* connection) override {
            if (OnDisconnectedFunc) OnDisconnectedFunc(connection);
        }

        template<typename Resp, typename ... Args>
        void Call(std::string func, Resp&& resp, Args&& ... args) {
            if (Connection)
            {
                RpcDispatcher.SendRpcRequest(Connection.get(), func, std::forward<Resp>(resp), std::forward<Args>(args)...);
            }
        }

        FRpcDispatcher RpcDispatcher;

        std::function<void(FTcpConnection*)> OnConnectedFunc;
        std::function<void(FTcpConnection*)> OnDisconnectedFunc;

    };

}
