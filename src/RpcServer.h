#pragma once
#include "TcpServer.h"
#include "RpcDispatcher.h"
#include "RpcConnection.h"

namespace Cpp
{
    class FRpcServer : public FTcpServer 
    {
    protected:
        struct FImpl : public FTcpServer::FImpl {
        public:
            FImpl(asio::io_context& ioContext)
                : FTcpServer::FImpl(ioContext)
            {}
            virtual std::shared_ptr<FTcpConnection> NewConnection() override {
                return std::make_shared<FRpcConnection>(shared_from_this());
            }
        };
    public:
        FRpcServer(asio::io_context& ioContext)
            : FTcpServer(ioContext, std::make_shared<FImpl>(ioContext))
            , RpcDispatcher(Impl)
        {
            Impl->RecvDataFunc = [this](FTcpConnection* connection, const char* data, std::size_t size) {
                RpcDispatcher.RecvRpc(connection, data, size); 
            };
        }

        ~FRpcServer() {
        }

        template<typename Resp, typename ... Args>
        void Call(asio::ip::address_v4 address, std::string func, Resp&& resp, Args&& ... args) {
            auto keyComp = ConnectionMap.key_comp();
            FConnectionId begin = std::pair(address.to_uint(), asio::ip::port_type(0));
            FConnectionId end = std::pair(address.to_uint(), asio::ip::port_type(-1));
            for (auto it = ConnectionMap.lower_bound(begin); it != ConnectionMap.end() && !keyComp(end, it->first); it++)
            {
                if (it->second)
                {
                    RpcDispatcher.SendRpcRequest(it->second.get(), func, std::forward<Resp>(resp), std::forward<Args>(args)...);
                }
            }
        }

        template<typename Resp, typename ... Args>
        void Call(std::shared_ptr<FTcpConnection> connection, std::string func, Resp&& resp, Args&& ... args) {
            if (connection)
            {
                RpcDispatcher.SendRpcRequest(connection.get(), func, std::forward<Resp>(resp), std::forward<Args>(args)...);
            }
        }

        FRpcDispatcher RpcDispatcher;

    };

}



















