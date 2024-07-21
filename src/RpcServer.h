#pragma once
#include "TcpServer.h"
#include "RpcDispatcher.h"
#include "RpcConnection.h"

namespace Cpp {
    class FRpcServer : public FTcpServer {
    public:
        FRpcServer(asio::io_context& ioContext)
            : FTcpServer(ioContext)
        {}

        ~FRpcServer() {}

        virtual void OnRecvData(FTcpConnection* connection, const char* data, std::size_t size)
        {
            json::value callableValue = json::parse(std::string_view(data, size));
            auto& callableArgs = callableValue.as_array();
            BOOST_ASSERT(callableArgs.size() == 3);
            int64_t requestId = callableArgs[0].get_int64();
            const char* func = callableArgs[1].get_string().c_str();
            auto it = RpcDispatcher.FuncMap.find(func);
            if (it != RpcDispatcher.FuncMap.end()) {
                it->second(callableArgs[2]);
            }
        }

        virtual std::shared_ptr<FTcpConnection> NewConnection() {
            return std::make_shared<FRpcConnection>(this);
        }

        FRpcDispatcher RpcDispatcher;
        std::atomic<int64_t> RequestCounter;
        std::unordered_map<int64_t, std::function<void()>> RequestMap;
    };

}



















