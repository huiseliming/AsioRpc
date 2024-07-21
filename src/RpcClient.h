#pragma once
#include "TcpContext.h"
#include "RpcDispatcher.h"
#include "RpcConnection.h"

namespace Cpp {

    class FRpcClient : public FTcpClient {
    public:
        FRpcClient(asio::io_context& ioContext)
            : FTcpClient(ioContext)
        {}

        ~FRpcClient() {}

        virtual std::shared_ptr<FTcpConnection> NewConnection(asio::ip::address address, asio::ip::port_type port) {
            return std::make_shared<FRpcConnection>(this, Strand, asio::ip::tcp::endpoint(address, port));
        }

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

        template<typename ... Args>
        void Call(std::string func, Args&& ... args) {
            if (Connection)
            {
                int64_t requestId = RequestCounter.fetch_add(1, std::memory_order_relaxed);
                RequestMap.insert(std::pair(requestId, [] {; }));
                asio::co_spawn(Strand, AsyncCall(Connection, requestId, std::move(func), std::make_tuple(args...)), asio::detached);
            }
        }

        template<typename ... Args>
        asio::awaitable<void> AsyncCall(std::shared_ptr<FTcpConnection> connection, int64_t requestId, std::string func, std::tuple<Args...> args) {
            std::string callableString = json::serialize(json::value_from(std::make_tuple(requestId, func, args)));
            uint32_t bufferSize = callableString.size();
            std::vector<uint8_t> buffer;
            buffer.resize(sizeof(uint32_t) + bufferSize);
            *reinterpret_cast<uint32_t*>(buffer.data()) = EndianCast(bufferSize);
            std::memcpy(buffer.data() + sizeof(uint32_t), callableString.data(), bufferSize);
            connection->Write(buffer);
            co_return;
        }

        FRpcDispatcher RpcDispatcher;
        std::atomic<int64_t> RequestCounter;
        std::unordered_map<int64_t, std::function<void()>> RequestMap;
    };

}