#pragma once
#include "TcpConnection.h"

namespace Cpp{

    class FRpcDispatcher {
    public:
        FRpcDispatcher(ITcpContext* tcpContext) 
            : TcpContext(tcpContext)
            , Strand(asio::make_strand(tcpContext->RefIoContext()))
        {

        }

        template<typename Func>
        bool AddFunc(std::string name, Func&& func) {
            using FuncRet = boost::callable_traits::return_type_t<Func>;
            using FuncArgs = boost::callable_traits::args_t<Func>;
            auto f = [func = std::forward<Func>(func)](json::value& args) -> json::value {
                if constexpr (std::is_void_v<FuncRet>)
                {
                     std::apply(func, json::value_to<FuncArgs>(args));
                     return json::value();
                }
                else
                {
                    return json::value_from(std::apply(func, json::value_to<FuncArgs>(args)));
                }
            };
            return RequestMap.insert(std::make_pair(name, f)).second;
        }

        void RecvRpc(FTcpConnection* connection, const char* data, std::size_t size) {
            json::value callableValue = json::parse(std::string_view(data, size));
            auto& callableArgs = callableValue.as_array();
            BOOST_ASSERT(callableArgs.size() == 3);
            int64_t id = callableArgs[0].get_int64();
            if (callableArgs[1].is_null())
            {
                auto it = ResponseMap.find(id);
                if (it != ResponseMap.end()) {
                    it->second(callableArgs[2]);
                }
            }
            else
            {
                const char* func = callableArgs[1].get_string().c_str();
                auto it = RequestMap.find(func);
                if (it != RequestMap.end()) {
                    asio::co_spawn(connection->RefStrand(), AsyncSendRpcResponse(connection->shared_from_this(), id, it->second(callableArgs[2])), asio::detached);
                }
            }
        }

        template<typename ... Args>
        BOOST_INLINE_CONSTEXPR void SendRpcRequest(FTcpConnection* connection, std::string func, Args&& ... args) {
            asio::co_spawn(Strand, AsyncSendRpcRequest(connection->shared_from_this(), std::move(func), std::make_tuple(std::forward<Args>(args)...)), asio::detached);
        }
    protected:
        asio::awaitable<void> AsyncSendRpcData(std::shared_ptr<FTcpConnection> connection, json::value rpcData) {
            std::string respValueString = json::serialize(rpcData);
            uint32_t bufferSize = respValueString.size();
            std::vector<uint8_t> buffer;
            buffer.resize(sizeof(uint32_t) + bufferSize);
            *reinterpret_cast<uint32_t*>(buffer.data()) = EndianCast(bufferSize);
            std::memcpy(buffer.data() + sizeof(uint32_t), respValueString.data(), bufferSize);
            connection->Write(std::move(connection), buffer);
            co_return;
        }

        asio::awaitable<void> AsyncSendRpcResponse(std::shared_ptr<FTcpConnection> connection, int64_t id, json::value respValue) {
            return AsyncSendRpcData(std::move(connection), json::array({ id, json::value(), respValue }));
        }

        asio::awaitable<void> AsyncSendRpcRequest(std::shared_ptr<FTcpConnection> connection, int64_t id, std::string func, json::value requestValue) {
            return AsyncSendRpcData(std::move(connection), json::array({ id, func, requestValue }));
        }
        
        template<typename ... Args>
        asio::awaitable<void> AsyncSendRpcRequest(std::shared_ptr<FTcpConnection> connection, std::string func, std::tuple<Args...> args) {
            BOOST_ASSERT(Strand.running_in_this_thread());
            int64_t id = IndexGenerator.fetch_add(1, std::memory_order_relaxed);
            ResponseMap.insert(std::pair(id, [](json::value&) { std::cout << "test resp" << std::endl; }));
            co_await AsyncSendRpcData(std::move(connection), json::array({ id, func, json::value_from(args) }));
        }

        ITcpContext* TcpContext;
        asio::strand<asio::io_context::executor_type> Strand;
        std::unordered_map<std::string, std::function<json::value(json::value&)>> RequestMap;
        std::atomic<int64_t> IndexGenerator;
        std::unordered_map<int64_t, std::function<void(json::value&)>> ResponseMap;
    };
}
