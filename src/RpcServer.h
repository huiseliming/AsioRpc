#pragma once
#include "TcpServer.h"
#include "RpcDispatcher.h"
#include "RpcConnection.h"

namespace Cpp
{

    class FRpcServer : public FTcpServer, public std::enable_shared_from_this<FRpcServer>
    {
    protected:
        struct FImpl : public FTcpServer::FImpl 
        {
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
            , RpcDispatcher(std::make_shared<FRpcDispatcher>(Impl))
            , Strand(asio::make_strand(ioContext))
        {
            Impl->InitFunc = [this] {
                Impl->ConnectedFunc = [rpcDispatcher = RpcDispatcher](FTcpConnection* rawConnection) {
                    {
                        std::lock_guard<std::mutex> lock(rpcDispatcher->Mutex);
                        rpcDispatcher->ConnectionMap.insert(std::make_pair(rawConnection->GetId(), rawConnection->shared_from_this()));
                    }
                    rpcDispatcher->OnAttached(rawConnection);
                };
                Impl->DisconnectedFunc = [rpcDispatcher = RpcDispatcher](FTcpConnection* rawConnection) {
                    rpcDispatcher->OnDetached(rawConnection);
                    {
                        std::lock_guard<std::mutex> lock(rpcDispatcher->Mutex);
                        auto it = rpcDispatcher->ConnectionMap.find(rawConnection->GetId());
                        if (it != rpcDispatcher->ConnectionMap.end() && it->second.get() == rawConnection)
                        {
                            rpcDispatcher->ConnectionMap.erase(it);
                        }
                    }
                };
                Impl->RecvDataFunc = [rpcDispatcher = RpcDispatcher](FTcpConnection* connection, std::vector<uint8_t> buffer) {
                    rpcDispatcher->RecvRpc(connection, (const char*)buffer.data(), buffer.size());
                };
            };
        }

        ~FRpcServer() { }

        std::shared_ptr<FRpcDispatcher>& RefRpcDispatcher() { return RpcDispatcher; }

    protected:

    protected:
        std::shared_ptr<FRpcDispatcher> RpcDispatcher;
        asio::strand<asio::io_context::executor_type> Strand;

    };

}



















