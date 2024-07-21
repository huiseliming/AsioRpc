#pragma once
#include "TcpServer.h"
#include "RpcDispatcher.h"
#include "RpcConnection.h"

namespace Cpp {
    class FRpcServer : public FTcpServer {
    public:
        FRpcServer(asio::io_context& ioContext)
            : FTcpServer(ioContext)
            , RpcDispatcher(this)
        {}

        ~FRpcServer() {}

        virtual void OnRecvData(FTcpConnection* connection, const char* data, std::size_t size)
        {
            RpcDispatcher.RecvRpc(connection, data, size);
        }

        virtual std::shared_ptr<FTcpConnection> NewConnection() {
            return std::make_shared<FRpcConnection>(this);
        }

        FRpcDispatcher RpcDispatcher;
    };

}



















