#pragma once
#include "TcpConnection.h"

namespace Cpp {

    template<typename T>
    struct IsAsioAwaitable : std::false_type { };

    template<typename T>
    struct IsAsioAwaitable<asio::awaitable<T>> : std::true_type { };

    class FRpcDispatcher : public std::enable_shared_from_this<FRpcDispatcher> {
    public:

        template<typename Func>
        static std::function<asio::awaitable<void>(json::value)> ToRequestFunc(Func&& func) {
            return [func = std::forward<Func>(func)](json::value val) -> asio::awaitable<void> {
                using FuncReturnType = boost::callable_traits::return_type_t<Func>;
                using FuncArgsType = boost::callable_traits::args_t<Func>;
                if constexpr (std::tuple_size_v<FuncArgsType>)
                {
                    if constexpr (IsAsioAwaitable<FuncReturnType>::value)
                        co_await func(json::value_to<std::decay_t<std::tuple_element_t<0, FuncArgsType>>>(val));
                    else
                        func(json::value_to<std::decay_t<std::tuple_element_t<0, FuncArgsType>>>(val));
                }
                else
                {
                    if constexpr (IsAsioAwaitable<FuncReturnType>::value)
                        co_await func();
                    else
                        func();
                }
                co_return;
                };
        }

    public:
        FRpcDispatcher(std::shared_ptr<ITcpContext> tcpContext)
            : TcpContext(std::move(tcpContext))
            , Strand(asio::make_strand(TcpContext->IoContext))
        {}

        void OnAttached(FTcpConnection* rawConnection) { if (AttachedFunc) AttachedFunc(rawConnection); }
        void OnDetached(FTcpConnection* rawConnection) { if (DetachedFunc) DetachedFunc(rawConnection); }
        void SetAttachedFunc(std::function<void(FTcpConnection*)> func) { AttachedFunc = func; }
        void SetDetachedFunc(std::function<void(FTcpConnection*)> func) { DetachedFunc = func; }

        template<typename Func>
        BOOST_FORCEINLINE bool AddFunc(std::string name, Func&& func) {
            using FuncReturnType = boost::callable_traits::return_type_t<Func>;
            using FuncArgs = boost::callable_traits::args_t<Func>;
            return RequestMap.insert(std::make_pair(name, [func = std::forward<Func>(func)](json::value args) -> asio::awaitable<json::value> {
                json::value value;
                if constexpr (IsAsioAwaitable<FuncReturnType>::value)
                {
                    if constexpr (!std::is_same_v<FuncReturnType, asio::awaitable<void>>)
                        value = json::value_from(co_await std::apply(func, json::value_to<FuncArgs>(args)));
                    else
                        co_await std::apply(func, json::value_to<FuncArgs>(args));
                }
                else
                {
                    if constexpr (!std::is_void_v<FuncReturnType>)
                        value = json::value_from(std::apply(func, json::value_to<FuncArgs>(args)));
                    else
                        std::apply(func, json::value_to<FuncArgs>(args));
                }
                co_return value;
                })).second;
        }

        template<typename Func, typename ... Args>
        BOOST_FORCEINLINE void Call(std::string address, std::string name, Func&& func, Args&& ... args) {
            std::lock_guard<std::mutex> lock(Mutex);
            auto keyComp = ConnectionMap.key_comp();
            FTcpConnection::IdType begin = std::pair(address, uint16_t(0));
            FTcpConnection::IdType end = std::pair(address, uint16_t(-1));
            for (auto it = ConnectionMap.lower_bound(begin); it != ConnectionMap.end() && !keyComp(end, it->first); it++)
            {
                if (it->second)
                {
                    asio::co_spawn(Strand, AsyncCall(shared_from_this(), it->second, std::move(name), FRpcDispatcher::ToRequestFunc(std::forward<Func>(func)), std::make_tuple(std::forward<Args>(args)...)), asio::detached);
                }
            }
        }

        template<typename Func, typename ... Args>
        BOOST_FORCEINLINE void Call(FTcpConnection::IdType id, std::string name, Func&& func, Args&& ... args) {
            std::lock_guard<std::mutex> lock(Mutex);
            auto it = ConnectionMap.find(id);
            if (it != ConnectionMap.end())
            {
                asio::co_spawn(Strand, AsyncCall(shared_from_this(), it->second, std::move(name), FRpcDispatcher::ToRequestFunc(std::forward<Func>(func)), std::make_tuple(std::forward<Args>(args)...)), asio::detached);
            }
        }

        template<typename Func, typename ... Args>
        BOOST_FORCEINLINE void Call(std::shared_ptr<FTcpConnection> connection, std::string name, Func&& func, Args&& ... args) {
            BOOST_ASSERT(Connection);
            std::lock_guard<std::mutex> lock(Mutex);
            asio::co_spawn(Strand, AsyncCall(shared_from_this(), connection ? std::move(connection) : Connection, std::move(name), FRpcDispatcher::ToRequestFunc(std::forward<Func>(func)), std::make_tuple(std::forward<Args>(args)...)), asio::detached);
        }

        template<typename Func, typename ... Args>
        asio::awaitable<void> AsyncCall(std::shared_ptr<FRpcDispatcher> self, std::shared_ptr<FTcpConnection> connection, std::string name, Func func, std::tuple<Args...> args) {
            try
            {
                int64_t id = IndexGenerator.fetch_add(1, std::memory_order_relaxed);
                co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
                ResponseMap.insert(std::pair(id, std::move(func)));
                if constexpr (sizeof...(Args) == 0)
                    SendRpcData(std::move(connection), json::array({ id, name, json::value() }));
                else
                    SendRpcData(std::move(connection), json::array({ id, name, json::value_from(args) }));
            }
            catch (const std::exception& e)
            {
                TcpContext->Log(fmt::format("FRpcDispatcher::AsyncCall[{}:{}] > exception : {}", connection->RefEndpoint().address().to_string(), connection->RefEndpoint().port(), e.what()).c_str());
            }
        }

    protected:
        BOOST_FORCEINLINE void SendRpcData(std::shared_ptr<FTcpConnection> connection, json::value rpcData) {
            std::string respValueString = json::serialize(rpcData);
            uint32_t bufferSize = respValueString.size();
            std::vector<uint8_t> buffer;
            buffer.resize(sizeof(uint32_t) + bufferSize);
            *reinterpret_cast<uint32_t*>(buffer.data()) = EndianCast(bufferSize);
            std::memcpy(buffer.data() + sizeof(uint32_t), respValueString.data(), bufferSize);
            connection->Write(std::move(connection), buffer);
        }

        asio::awaitable<void> AsyncRecvRpcRequest(std::shared_ptr<FTcpConnection> connection, json::array rpcData) {
            try
            {
                int64_t id = rpcData[0].get_int64();
                const char* func = rpcData[1].get_string().c_str();
                auto it = RequestMap.find(func);
                if (it != RequestMap.end()) {
                    json::value respValue = co_await it->second(rpcData[2]);
                    SendRpcData(connection->shared_from_this(), json::array({ id, json::value(), respValue }));
                }
            }
            catch (const std::exception& e)
            {
                TcpContext->Log(fmt::format("FRpcDispatcher::AsyncRecvRpcRequest[{}:{}] > exception : {}", connection->RefEndpoint().address().to_string(), connection->RefEndpoint().port(), e.what()).c_str());
            }
        }

        asio::awaitable<void> AsyncRecvRpcResponse(std::shared_ptr<FTcpConnection> connection, json::array rpcData) {
            try
            {
                co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
                int64_t id = rpcData[0].get_int64();
                auto it = ResponseMap.find(id);
                if (it != ResponseMap.end()) {
                    auto respFunc = std::move(it->second);
                    ResponseMap.erase(it);
                    co_await asio::dispatch(asio::bind_executor(connection->RefStrand(), asio::use_awaitable));
                    co_await respFunc(rpcData[2]);
                }
            }
            catch (const std::exception& e)
            {
                TcpContext->Log(fmt::format("FRpcDispatcher::AsyncRecvRpcResponse[{}:{}] > exception : {}", connection->RefEndpoint().address().to_string(), connection->RefEndpoint().port(), e.what()).c_str());
            }
        }

        BOOST_FORCEINLINE void RecvRpc(FTcpConnection* connection, const char* data, std::size_t size) {
            json::value rpcDataValue = json::parse(std::string_view(data, size));
            auto rpcData = std::move(rpcDataValue.as_array());
            BOOST_ASSERT(rpcData.size() == 3);
            if (rpcData[1].is_null())
            {
                asio::co_spawn(connection->RefStrand(), AsyncRecvRpcResponse(connection->shared_from_this(), std::move(rpcData)), asio::detached);
            }
            else
            {
                asio::co_spawn(connection->RefStrand(), AsyncRecvRpcRequest(connection->shared_from_this(), std::move(rpcData)), asio::detached);
            }
        }

    protected:
        std::shared_ptr<ITcpContext> TcpContext;
        asio::strand<asio::io_context::executor_type> Strand;
        std::unordered_map<std::string, std::function<asio::awaitable<json::value>(json::value)>> RequestMap;
        std::unordered_map<int64_t, std::function<asio::awaitable<void>(json::value)>> ResponseMap;
        std::atomic<int64_t> IndexGenerator;

        std::mutex Mutex;
        std::shared_ptr<FTcpConnection> Connection;
        std::map<FTcpConnection::IdType, std::shared_ptr<FTcpConnection>> ConnectionMap;
        std::function<void(FTcpConnection*)> AttachedFunc;
        std::function<void(FTcpConnection*)> DetachedFunc;

        friend class FRpcClient;
        friend class FRpcServer;
    };
}
