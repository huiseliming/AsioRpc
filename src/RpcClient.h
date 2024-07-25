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
            Impl->InitFunc = [this] {
                Impl->ConnectedFunc = [rpcDispatcher = RpcDispatcher, weakSelf = weak_from_this()](FTcpConnection* rawConnection) {
                    {
                        std::lock_guard<std::mutex> lock(rpcDispatcher->Mutex);
                        rpcDispatcher->Connection = rawConnection->shared_from_this();
                    }
                    rpcDispatcher->OnAttached(rawConnection);
                };
                Impl->DisconnectedFunc = [rpcDispatcher = RpcDispatcher, weakSelf = weak_from_this()](FTcpConnection* rawConnection) {
                    rpcDispatcher->OnDetached(rawConnection);
                    {
                        std::lock_guard<std::mutex> lock(rpcDispatcher->Mutex);
                        rpcDispatcher->Connection.reset();
                    }
                };
                Impl->RecvDataFunc = [rpcDispatcher = RpcDispatcher](FTcpConnection* connection, const char* data, std::size_t size) {
                    rpcDispatcher->RecvRpc(connection, data, size);
                };
            };
        }

        ~FRpcClient() { }

        std::shared_ptr<FRpcDispatcher>& RefRpcDispatcher() { return RpcDispatcher; }

    protected:
        std::shared_ptr<FRpcDispatcher> RpcDispatcher;
        asio::strand<asio::io_context::executor_type> Strand;

    };

}
