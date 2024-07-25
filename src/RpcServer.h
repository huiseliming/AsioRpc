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
                Impl->RecvDataFunc = [rpcDispatcher = RpcDispatcher](FTcpConnection* connection, const char* data, std::size_t size) {
                    rpcDispatcher->RecvRpc(connection, data, size);
                };
            };
        }

        ~FRpcServer() { }

        std::shared_ptr<FRpcDispatcher>& RefRpcDispatcher() { return RpcDispatcher; }

        //template<typename Func, typename ... Args>
        //void Call(asio::ip::address_v4 address, std::string name, Func&& func, Args&& ... args) {
        //    asio::co_spawn(Strand, AsyncCall(shared_from_this(), std::move(address), std::move(name), FRpcDispatcher::ToRequestFunc(std::forward<Func>(func)), std::make_tuple(std::forward<Args>(args)...)), asio::detached);
        //}

        template<typename Func, typename ... Args>
        void Call(std::shared_ptr<FTcpConnection> connection, std::string name, Func&& func, Args&& ... args) {
            asio::co_spawn(RpcDispatcher->Strand, RpcDispatcher->AsyncCall(RpcDispatcher, std::move(connection), std::move(name), FRpcDispatcher::ToRequestFunc(std::forward<Func>(func)), std::make_tuple(std::forward<Args>(args)...)), asio::detached);
        }

        //template<typename ... Args>
        //asio::awaitable<void> AsyncCall(std::shared_ptr<FRpcServer> self, asio::ip::address_v4 address, std::string name, std::function<asio::awaitable<void>(json::value)> func, std::tuple<Args...> args) {
        //    BOOST_ASSERT(Strand.running_in_this_thread());
        //    auto keyComp = ConnectionMap.key_comp();
        //    FConnectionId begin = std::pair(address.to_uint(), asio::ip::port_type(0));
        //    FConnectionId end = std::pair(address.to_uint(), asio::ip::port_type(-1));
        //    for (auto it = ConnectionMap.lower_bound(begin); it != ConnectionMap.end() && !keyComp(end, it->first); it++)
        //    {
        //        if (it->second)
        //        {
        //            co_await RpcDispatcher->AsyncCall(RpcDispatcher, it->second, name, std::move(func), args);
        //        }
        //    }
        //}

    protected:

    protected:
        std::shared_ptr<FRpcDispatcher> RpcDispatcher;
        asio::strand<asio::io_context::executor_type> Strand;

    };

}



















