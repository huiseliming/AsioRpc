#pragma once
#include "TcpServer.h"
#include "RpcDispatcher.h"
#include "RpcConnection.h"

namespace Cpp
{
    class FRpcServer : public FTcpServer {
    public:
        FRpcServer(asio::io_context& ioContext)
            : FTcpServer(ioContext)
            , RpcDispatcher(this)
        {}

        ~FRpcServer() {}

        virtual std::shared_ptr<FTcpConnection> NewConnection() {
            return std::make_shared<FRpcConnection>(this);
        }

        virtual void OnConnected(FTcpConnection* connection) {
            if (OnConnectedFunc) OnConnectedFunc(connection);
        }

        virtual void OnRecvData(FTcpConnection* connection, const char* data, std::size_t size) {
            RpcDispatcher.RecvRpc(connection, data, size);
        }

        virtual void OnDisconnected(FTcpConnection* connection) {
            if (OnDisconnectedFunc) OnDisconnectedFunc(connection);
        }

        FRpcDispatcher RpcDispatcher;

        std::function<void(FTcpConnection*)> OnConnectedFunc;
        std::function<void(FTcpConnection*)> OnDisconnectedFunc;

    };

}



















