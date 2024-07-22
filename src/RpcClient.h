#pragma once
#include "TcpClient.h"
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
        {
            RecvDataFunc = [this](FTcpConnection* connection, const char* data, std::size_t size) {
                RpcDispatcher.RecvRpc(connection, data, size);
            };
        }

        ~FRpcClient() {
            Stop();
        }

        virtual std::shared_ptr<FTcpConnection> NewConnection(asio::ip::address address, asio::ip::port_type port) {
            return std::make_shared<FRpcConnection>(this, Strand, asio::ip::tcp::endpoint(address, port));
        }

        template<typename Resp, typename ... Args>
        void Call(std::string func, Resp&& resp, Args&& ... args) {
            if (Connection)
            {
                RpcDispatcher.SendRpcRequest(Connection.get(), func, std::forward<Resp>(resp), std::forward<Args>(args)...);
            }
        }

        FRpcDispatcher RpcDispatcher;
    };

}
