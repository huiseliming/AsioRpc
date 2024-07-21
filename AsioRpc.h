//#pragma once
//#include <memory>
//#include <vector>
//#include <string>
//#include <map>
//#include <queue>
//#include <iostream>
//#include <functional>
//#include <unordered_map>
//#include <boost/asio.hpp>
//#include <boost/endian/conversion.hpp>
//#include <boost/json.hpp>
//#include <boost/callable_traits.hpp>
//
//namespace Cpp {
//    using namespace boost;
//    using FConnectionId = std::pair<asio::ip::address_v4::uint_type, asio::ip::port_type>;
//
//    class FTcpConnection;
//
//    template <typename T>
//    inline T EndianCast(const T& val)
//    {
//        if constexpr (std::endian::native == std::endian::little) {
//            return boost::endian::endian_reverse(val);
//        }
//        return val;
//    }
//
//    class ITcpContext 
//    {
//    public:
//        ITcpContext(asio::io_context& ioContext)
//            : IoContext(ioContext)
//        {}
//
//        asio::io_context& RefIoContext() { return IoContext; }
//
//        virtual void OnConnected(FTcpConnection* connection) { }
//        virtual void OnRecvData(FTcpConnection* connection, const char* data, std::size_t size) { }
//        virtual void OnDisconnected(FTcpConnection* connection) { }
//
//    protected:
//        asio::io_context& IoContext;
//
//    };
//
//	class FTcpConnection : public std::enable_shared_from_this<FTcpConnection> 
//    {
//    public:
//        FTcpConnection(ITcpContext* tcpContext)
//            : TcpContext(tcpContext)
//            , Strand(asio::make_strand(tcpContext->RefIoContext()))
//            , Socket(Strand)
//        {}
//
//        FTcpConnection(ITcpContext* tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
//            : TcpContext(tcpContext)
//            , Strand(strand)
//            , Socket(strand)
//            , Endpoint(endpoint)
//        {}
//
//        ~FTcpConnection() {
//            if (CleanupFunc)
//            {
//                CleanupFunc();
//                CleanupFunc = nullptr;
//            }
//        }
//
//        FConnectionId GetId() { return std::make_pair(Endpoint.address().to_v4().to_uint(), Endpoint.port()); }
//
//        void Read() { asio::co_spawn(Strand, AsyncRead(shared_from_this()), asio::detached); }
//        void Write(std::vector<uint8_t> data) { asio::co_spawn(Strand, AsyncWrite(shared_from_this(), std::move(data)), asio::detached); }
//        void Close() { asio::co_spawn(Strand, AsyncClose(shared_from_this()), asio::detached); }
//
//        asio::ip::tcp::socket& RefSocket() { return Socket; }
//        asio::strand<asio::io_context::executor_type>& RefStrand() { return Strand; }
//        asio::ip::tcp::endpoint& RefEndpoint() { return Endpoint; }
//        std::queue<std::vector<uint8_t>>& RefWriteQueue() { return WriteQueue; }
//
//    protected:
//        template<typename Func>
//        void SetCleanupFunc(Func&& func) {
//            CleanupFunc = std::forward<Func>(func);
//        }
//
//        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection)
//        {
//            BOOST_ASSERT(Strand.running_in_this_thread());
//            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: connected" << std::endl;
//            try
//            {
//                char buffer[4 * 1024];
//                for (;;)
//                {
//                    auto bytesTransferred = co_await Socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
//                    BOOST_ASSERT(Strand.running_in_this_thread());
//                    printf("client: ");
//                    for (size_t i = 0; i < bytesTransferred; i++)
//                    {
//                        printf("%c", buffer[i]);
//                    }
//                    printf("\n");
//                }
//            }
//            catch (const std::exception& e)
//            {
//                Socket.close();
//                std::cout << "exception: " << e.what() << std::endl;
//            }
//            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: disconnected" << std::endl;
//        }
//
//        virtual asio::awaitable<void> AsyncWrite(std::shared_ptr<FTcpConnection> connection, std::vector<uint8_t> data)
//        {
//            BOOST_ASSERT(Strand.running_in_this_thread());
//            try
//            {
//                bool bIsWriteQueueEmpty = WriteQueue.empty();
//                WriteQueue.push(std::move(data));
//                if (!bIsWriteQueueEmpty)
//                    co_return;
//                while (!WriteQueue.empty())
//                {
//                    auto bytesTransferred = co_await Socket.async_write_some(asio::buffer(WriteQueue.front()), asio::use_awaitable);
//                    BOOST_ASSERT(Strand.running_in_this_thread());
//                    WriteQueue.pop();
//                }
//            }
//            catch (const std::exception& e)
//            {
//                Socket.close();
//                std::cout << "exception: " << e.what() << std::endl;
//            }
//        }
//
//        asio::awaitable<void> AsyncClose(std::shared_ptr<FTcpConnection> self)
//        {
//            BOOST_ASSERT(Strand.running_in_this_thread());
//            co_return Socket.close();
//        }
//
//    protected:
//        ITcpContext* TcpContext;
//        asio::strand<asio::io_context::executor_type> Strand;
//        asio::ip::tcp::socket Socket;
//        asio::ip::tcp::endpoint Endpoint;
//        std::queue<std::vector<uint8_t>> WriteQueue;
//        std::function<void()> CleanupFunc;
//
//        friend class FTcpServer;
//        friend class FTcpClient;
//	};
//
//
//
//    class FTcpServer : public ITcpContext {
//        //using FTcpConnection = FTcpConnection;
//    public:
//        FTcpServer(asio::io_context& ioContext)
//            : ITcpContext(ioContext)
//            , Strand(asio::make_strand(ioContext))
//        {}
//
//        ~FTcpServer() {
//            Stop();
//        }
//
//        virtual std::shared_ptr<FTcpConnection> NewConnection() {
//            return std::make_shared<FTcpConnection>(this);
//        }
//
//        void Start(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
//        {
//            Stop();
//            auto acceptor = std::make_shared<asio::ip::tcp::acceptor>(Strand, asio::ip::tcp::endpoint(address, port));
//            Acceptor = acceptor;
//            asio::co_spawn(Strand, AsyncAccept(acceptor), asio::detached);
//        }
//
//        void Stop()
//        {
//            if (std::shared_ptr<asio::ip::tcp::acceptor> acceptor = Acceptor.lock()) {
//                asio::post(Strand, [acceptor = std::move(acceptor)] { acceptor->close(); });
//            }
//            while (!Acceptor.expired()) {
//                std::this_thread::yield();
//            }
//            Acceptor.reset();
//            asio::co_spawn(Strand, [=]() -> asio::awaitable<void> {
//                BOOST_ASSERT(Strand.running_in_this_thread());
//                for (auto& [id, connectionWeakPtr] : ConnectionMap)
//                {
//                    if (auto connection = connectionWeakPtr.lock())
//                    {
//                        connection->Close();
//                    }
//                }
//                asio::steady_timer timer(Strand);
//                while (!ConnectionMap.empty())
//                {
//                    timer.expires_after(std::chrono::milliseconds(1));
//                    co_await timer.async_wait(asio::use_awaitable);
//                    BOOST_ASSERT(Strand.running_in_this_thread());
//                }
//            }, asio::use_future).get();
//        }
//
//        virtual asio::awaitable<void> AsyncAccept(std::shared_ptr<asio::ip::tcp::acceptor> acceptor) {
//            try
//            {
//                for (;;)
//                {
//                    std::shared_ptr<FTcpConnection> connection = NewConnection();
//                    co_await acceptor->async_accept(connection->RefSocket(), connection->RefEndpoint(), asio::use_awaitable);
//                    if (!connection->RefEndpoint().address().is_v4())
//                    {
//                        continue;
//                    }
//                    co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
//                    auto connectionId = connection->GetId();
//                    auto insertResult = ConnectionMap.insert(std::make_pair(connectionId, connection));
//                    if (insertResult.second)
//                    {
//                        connection->SetCleanupFunc([=, this] {
//                            asio::co_spawn(Strand, [=, this]() -> asio::awaitable<void> {
//                                BOOST_ASSERT(Strand.running_in_this_thread());
//                                ConnectionMap.erase(connectionId);
//                                co_return;
//                            }, asio::detached);
//                        });
//                        connection->Read();
//                    }
//                }
//            }
//            catch (const std::exception& e)
//            {
//                std::cout << "exception: " << e.what() << std::endl;
//            }
//        }
//
//    protected:
//        std::weak_ptr<asio::ip::tcp::acceptor> Acceptor;
//        asio::strand<asio::io_context::executor_type> Strand;
//        std::map<FConnectionId, std::weak_ptr<FTcpConnection>> ConnectionMap;
//    };
//
//    class FTcpClient : public ITcpContext
//    {
//    public:
//
//        FTcpClient(asio::io_context& ioContext)
//            : ITcpContext(ioContext)
//            , Strand(asio::make_strand(ioContext))
//        {}
//
//        ~FTcpClient() {
//            Stop();
//        }
//
//        virtual std::shared_ptr<FTcpConnection> NewConnection(asio::ip::address address, asio::ip::port_type port) {
//            return std::make_shared<FTcpConnection>(this, Strand, asio::ip::tcp::endpoint(address, port));
//        }
//
//        std::future<std::shared_ptr<FTcpConnection>> Start(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
//        {
//            return asio::co_spawn(Strand, AsyncConnect(address, port), asio::use_future);
//        }
//
//        void Stop()
//        {
//            asio::co_spawn(Strand, AsyncStop(ConnectionWeakPtr), asio::use_future).get();
//        }
//
//        asio::awaitable<void> AsyncStop(std::weak_ptr<FTcpConnection> connectionWeakPtr)
//        {
//            co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
//            if (auto connection = connectionWeakPtr.lock()) {
//                connection->Close();
//            }
//            asio::steady_timer timer(Strand);
//            while (!connectionWeakPtr.expired()) {
//                timer.expires_after(std::chrono::milliseconds(1));
//                co_await timer.async_wait(asio::use_awaitable);
//                BOOST_ASSERT(Strand.running_in_this_thread());
//            }
//        }
//
//        asio::awaitable<std::shared_ptr<FTcpConnection>> AsyncConnect(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
//        {
//            try {
//                co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
//                if (!ConnectionWeakPtr.expired())
//                {
//                    co_return nullptr;
//                }
//                std::shared_ptr<FTcpConnection> connection = NewConnection(address, port);
//                ConnectionWeakPtr = connection;
//
//                asio::deadline_timer deadlineTimer(connection->RefStrand());
//                deadlineTimer.expires_from_now(boost::posix_time::seconds(3));
//                deadlineTimer.async_wait([&](boost::system::error_code errorCode) { if (!errorCode) connection->RefSocket().close(); });
//                co_await connection->RefSocket().async_connect(connection->RefEndpoint(), asio::use_awaitable);
//                deadlineTimer.cancel();
//                if (connection->RefSocket().is_open())
//                {
//                    asio::co_spawn(connection->Strand, [=]() -> asio::awaitable<void> {
//                        BOOST_ASSERT(Strand.running_in_this_thread());
//                        Connection = connection;
//                        co_await connection->AsyncRead(connection);
//                        co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
//                        Connection.reset();
//                    }, asio::detached);
//                    co_return connection;
//                }
//            }
//            catch (const std::exception& e) {
//                std::cout << "exception: " << e.what() << std::endl;
//            }
//            co_return nullptr;
//        }
//
//    protected:
//        asio::strand<asio::io_context::executor_type> Strand;
//        std::shared_ptr<FTcpConnection> Connection;
//        std::weak_ptr<FTcpConnection> ConnectionWeakPtr;
//
//    };
//
//
//    class FRpcDispatcher {
//    public:
//        FRpcDispatcher() {
//
//        }
//
//        template<typename Func>
//        bool AddFunc(std::string name, Func&& func) {
//            using FuncArgs = boost::callable_traits::args_t<Func>;
//            auto f = [func = std::forward<Func>(func)](json::value& args) {
//                std::apply(func, json::value_to<FuncArgs>(args));
//                };
//            return FuncMap.insert(std::make_pair(name, f)).second;
//        }
//
//        std::unordered_map<std::string, std::function<void(json::value&)>> FuncMap;
//    };
//
//    class FRpcConnection : public FTcpConnection {
//    public:
//        FRpcConnection(ITcpContext* tcpContext)
//            : FTcpConnection(tcpContext)
//        {}
//
//        FRpcConnection(ITcpContext* tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
//            : FTcpConnection(tcpContext, strand, endpoint)
//        {}
//
//        ~FRpcConnection() { }
//      
//        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection) override
//        {
//            BOOST_ASSERT(Strand.running_in_this_thread());
//            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: connected" << std::endl;
//            TcpContext->OnConnected(connection.get());
//            try
//            {
//                char buffer[4 * 1024];
//                for (;;)
//                {
//                    uint32_t bufferSize;
//                    auto bytesTransferred = co_await Socket.async_read_some(asio::buffer(&bufferSize, sizeof(bufferSize)), asio::use_awaitable);
//                    std::vector<char> buffer;
//                    buffer.resize(EndianCast(bufferSize));
//                    bytesTransferred = co_await Socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
//                    printf("recv : ");
//                    for (size_t i = 0; i < bytesTransferred; i++)
//                    {
//                        printf("%c", std::isprint(buffer[i]) ? buffer[i] : '?');
//                    }
//                    printf("\n");
//                    TcpContext->OnRecvData(connection.get(), buffer.data(), buffer.size());
//                }
//            }
//            catch (const std::exception& e)
//            {
//                Socket.close();
//                std::cout << "exception: " << e.what() << std::endl;
//            }
//            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: disconnected" << std::endl;
//            TcpContext->OnConnected(connection.get());
//        }
//
//    };
//
//    class FRpcServer : public FTcpServer {
//    public:
//        FRpcServer(asio::io_context& ioContext)
//            : FTcpServer(ioContext)
//        {}
//
//        ~FRpcServer() {}
//
//        virtual void OnRecvData(FTcpConnection* connection, const char* data, std::size_t size)
//        {
//            json::value callableValue = json::parse(std::string_view(data, size));
//            auto& callableArgs = callableValue.as_array();
//            BOOST_ASSERT(callableArgs.size() == 3);
//            int64_t requestId = callableArgs[0].get_int64();
//            const char* func = callableArgs[1].get_string().c_str();
//            auto it = RpcDispatcher.FuncMap.find(func);
//            if (it != RpcDispatcher.FuncMap.end()) {
//                it->second(callableArgs[2]);
//            }
//        }
//
//        virtual std::shared_ptr<FTcpConnection> NewConnection() {
//            return std::make_shared<FRpcConnection>(this);
//        }
//
//        FRpcDispatcher RpcDispatcher;
//        std::atomic<int64_t> RequestCounter;
//        std::unordered_map<int64_t, std::function<void()>> RequestMap;
//    };
//
//    class FRpcClient : public FTcpClient {
//    public:
//        FRpcClient(asio::io_context& ioContext)
//            : FTcpClient(ioContext)
//        {}
//
//        ~FRpcClient() {}
//
//        virtual std::shared_ptr<FTcpConnection> NewConnection(asio::ip::address address, asio::ip::port_type port) {
//            return std::make_shared<FRpcConnection>(this, Strand, asio::ip::tcp::endpoint(address, port));
//        }
//
//        virtual void OnRecvData(FTcpConnection* connection, const char* data, std::size_t size)
//        {
//            json::value callableValue = json::parse(std::string_view(data, size));
//            auto& callableArgs = callableValue.as_array();
//            BOOST_ASSERT(callableArgs.size() == 3);
//            int64_t requestId = callableArgs[0].get_int64();
//            const char* func = callableArgs[1].get_string().c_str();
//            auto it = RpcDispatcher.FuncMap.find(func);
//            if (it != RpcDispatcher.FuncMap.end()) {
//                it->second(callableArgs[2]);
//            }
//        }
//
//        template<typename ... Args>
//        void Call(std::string func, Args&& ... args) {
//            if (Connection)
//            {
//                int64_t requestId = RequestCounter.fetch_add(1, std::memory_order_relaxed);
//                RequestMap.insert(std::pair(requestId, [] {; }));
//                asio::co_spawn(Strand, AsyncCall(Connection, requestId, std::move(func), std::make_tuple(args...)), asio::detached);
//            }
//        }
//
//        template<typename ... Args>
//        asio::awaitable<void> AsyncCall(std::shared_ptr<FTcpConnection> connection, int64_t requestId, std::string func, std::tuple<Args...> args) {
//            std::string callableString = json::serialize(json::value_from(std::make_tuple(requestId, func, args)));
//            uint32_t bufferSize = callableString.size();
//            std::vector<uint8_t> buffer;
//            buffer.resize(sizeof(uint32_t) + bufferSize);
//            *reinterpret_cast<uint32_t*>(buffer.data()) = EndianCast(bufferSize);
//            std::memcpy(buffer.data() + sizeof(uint32_t), callableString.data(), bufferSize);
//            connection->Write(buffer);
//            co_return;
//        }
//
//        FRpcDispatcher RpcDispatcher;
//        std::atomic<int64_t> RequestCounter;
//        std::unordered_map<int64_t, std::function<void()>> RequestMap;
//    };
//
//}
//
