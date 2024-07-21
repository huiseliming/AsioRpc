#pragma once
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <queue>
#include <iostream>
#include <functional>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/json.hpp>
#include <boost/callable_traits.hpp>

namespace Cpp {
    using namespace boost;
    using FConnectionId = std::pair<asio::ip::address_v4::uint_type, asio::ip::port_type>;

    class FTcpConnection;

    template <typename T>
    inline T EndianCast(const T& val)
    {
        if constexpr (std::endian::native == std::endian::little) {
            return boost::endian::endian_reverse(val);
        }
        return val;
    }

    class ITcpContext 
    {
    public:
        ITcpContext(asio::io_context& ioContext)
            : IoContext(ioContext)
        {}

        asio::io_context& RefIoContext() { return IoContext; }

        void OnConnected(FTcpConnection* connection) { }
        void OnRecvData(FTcpConnection* connection, const char* data, std::size_t size) { }
        void OnDisconnected(FTcpConnection* connection) { }

    protected:
        asio::io_context& IoContext;

    };

	class FTcpConnection : public std::enable_shared_from_this<FTcpConnection> 
    {
    public:
        FTcpConnection(ITcpContext* tcpContext)
            : TcpContext(tcpContext)
            , Strand(asio::make_strand(tcpContext->RefIoContext()))
            , Socket(Strand)
        {}

        FTcpConnection(ITcpContext* tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
            : TcpContext(tcpContext)
            , Strand(strand)
            , Socket(Strand)
            , Endpoint(endpoint)
        {}

        ~FTcpConnection() {
            if (CleanupFunc)
            {
                CleanupFunc();
                CleanupFunc = nullptr;
            }
        }

        FConnectionId GetId() { return std::make_pair(Endpoint.address().to_v4().to_uint(), Endpoint.port()); }

        void Read() { asio::co_spawn(Strand, AsyncRead(shared_from_this()), asio::detached); }
        void Write(std::vector<uint8_t> data) { asio::co_spawn(Strand, AsyncWrite(shared_from_this(), std::move(data)), asio::detached); }
        void Close() { asio::co_spawn(Strand, AsyncClose(shared_from_this()), asio::detached); }

        asio::ip::tcp::socket& RefSocket() { return Socket; }
        asio::strand<asio::io_context::executor_type>& RefStrand() { return Strand; }
        asio::ip::tcp::endpoint& RefEndpoint() { return Endpoint; }
        std::queue<std::vector<uint8_t>>& RefWriteQueue() { return WriteQueue; }

    protected:
        template<typename Func>
        void SetCleanupFunc(Func&& func) {
            CleanupFunc = std::forward<Func>(func);
        }

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection)
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: connected" << std::endl;
            try
            {
                char buffer[4 * 1024];
                for (;;)
                {
                    auto bytesTransferred = co_await Socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
                    BOOST_ASSERT(Strand.running_in_this_thread());
                    printf("client: ");
                    for (size_t i = 0; i < bytesTransferred; i++)
                    {
                        printf("%c", buffer[i]);
                    }
                    printf("\n");
                    BOOST_ASSERT(Strand.running_in_this_thread());
                }
            }
            catch (const std::exception& e)
            {
                Socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: disconnected" << std::endl;
        }

        virtual asio::awaitable<void> AsyncWrite(std::shared_ptr<FTcpConnection> connection, std::vector<uint8_t> data)
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            try
            {
                bool bIsWriteQueueEmpty = WriteQueue.empty();
                WriteQueue.push(std::move(data));
                if (!bIsWriteQueueEmpty)
                    co_return;
                while (!WriteQueue.empty())
                {
                    auto bytesTransferred = co_await Socket.async_write_some(asio::buffer(WriteQueue.front()), asio::use_awaitable);
                    BOOST_ASSERT(Strand.running_in_this_thread());
                    WriteQueue.pop();
                }
            }
            catch (const std::exception& e)
            {
                Socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
        }

        asio::awaitable<void> AsyncClose(std::shared_ptr<FTcpConnection> self)
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            co_return Socket.close();
        }

    protected:
        ITcpContext* TcpContext;
        asio::strand<asio::io_context::executor_type> Strand;
        asio::ip::tcp::socket Socket;
        asio::ip::tcp::endpoint Endpoint;
        std::queue<std::vector<uint8_t>> WriteQueue;
        std::function<void()> CleanupFunc;

        friend class FTcpServer;
        friend class FTcpClient;
	};



    class FTcpServer : public ITcpContext {
        //using FTcpConnection = FTcpConnection;
    public:
        FTcpServer(asio::io_context& ioContext)
            : ITcpContext(ioContext)
            , Strand(asio::make_strand(ioContext))
        {}

        ~FTcpServer() {
            Stop();
        }

        void Start(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            Stop();
            auto acceptor = std::make_shared<asio::ip::tcp::acceptor>(Strand, asio::ip::tcp::endpoint(address, port));
            Acceptor = acceptor;
            asio::co_spawn(Strand, AsyncAccept(acceptor), asio::detached);
        }

        void Stop()
        {
            if (std::shared_ptr<asio::ip::tcp::acceptor> acceptor = Acceptor.lock()) {
                asio::post(Strand, [acceptor = std::move(acceptor)] { acceptor->close(); });
            }
            while (!Acceptor.expired()) {
                std::this_thread::yield();
            }
            Acceptor.reset();
            asio::co_spawn(Strand, [=]() -> asio::awaitable<void> {
                for (auto& [id, connectionWeakPtr] : ConnectionMap)
                {
                    if (auto connection = connectionWeakPtr.lock())
                    {
                        connection->Close();
                    }
                }
                asio::steady_timer timer(Strand);
                while (!ConnectionMap.empty())
                {
                    timer.expires_after(std::chrono::milliseconds(1));
                    co_await timer.async_wait(asio::use_awaitable);
                    BOOST_ASSERT(Strand.running_in_this_thread());
                }
                }, asio::use_future).get();
        }

        virtual asio::awaitable<void> AsyncAccept(std::shared_ptr<asio::ip::tcp::acceptor> acceptor) {
            try
            {
                for (;;)
                {
                    std::shared_ptr<FTcpConnection> connection = std::make_shared<FTcpConnection>(this);
                    co_await acceptor->async_accept(connection->RefSocket(), connection->RefEndpoint(), asio::use_awaitable);
                    if (!connection->RefEndpoint().address().is_v4())
                    {
                        continue;
                    }
                    co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
                    BOOST_ASSERT(Strand.running_in_this_thread());
                    auto connectionId = connection->GetId();
                    auto insertResult = ConnectionMap.insert(std::make_pair(connectionId, connection));
                    if (insertResult.second)
                    {
                        connection->SetCleanupFunc([=, this] {
                            asio::co_spawn(Strand, [=, this]() -> asio::awaitable<void> {
                                ConnectionMap.erase(connectionId);
                                co_return;
                            }, asio::detached);
                        });
                        connection->Read();
                    }
                }
            }
            catch (const std::exception& e)
            {
                std::cout << "exception: " << e.what() << std::endl;
            }
        }

    protected:
        std::weak_ptr<asio::ip::tcp::acceptor> Acceptor;
        asio::strand<asio::io_context::executor_type> Strand;
        std::map<FConnectionId, std::weak_ptr<FTcpConnection>> ConnectionMap;
    };

    class FTcpClient : public ITcpContext
    {
    public:

        FTcpClient(asio::io_context& ioContext)
            : ITcpContext(ioContext)
            , Strand(asio::make_strand(ioContext))
        {}

        ~FTcpClient() {
            Stop();
        }

        std::future<std::shared_ptr<FTcpConnection>> Start(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            return asio::co_spawn(Strand, AsyncConnect(address, port), asio::use_future);
        }

        void Stop()
        {
            asio::co_spawn(Strand, AsyncStop(ConnectionWeakPtr), asio::use_future).get();
        }

        asio::awaitable<void> AsyncStop(std::weak_ptr<FTcpConnection> connectionWeakPtr)
        {
            co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
            if (auto connection = connectionWeakPtr.lock()) {
                connection->Close();
            }
            asio::steady_timer timer(Strand);
            while (!connectionWeakPtr.expired()) {
                timer.expires_after(std::chrono::milliseconds(1));
                co_await timer.async_wait(asio::use_awaitable);
            }
        }

        asio::awaitable<std::shared_ptr<FTcpConnection>> AsyncConnect(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            try {
                co_await AsyncStop(ConnectionWeakPtr);
                BOOST_ASSERT(Strand.running_in_this_thread());
                std::shared_ptr<FTcpConnection> connection = std::make_shared<FTcpConnection>(this, Strand, asio::ip::tcp::endpoint(address, port));
                ConnectionWeakPtr = connection;

                asio::deadline_timer deadlineTimer(connection->RefStrand());
                deadlineTimer.expires_from_now(boost::posix_time::seconds(3));
                deadlineTimer.async_wait([&](boost::system::error_code errorCode) { if (!errorCode) connection->RefSocket().close(); });
                co_await connection->RefSocket().async_connect(connection->RefEndpoint(), asio::use_awaitable);
                BOOST_ASSERT(Strand.running_in_this_thread());
                deadlineTimer.cancel();
                if (connection->RefSocket().is_open())
                {
                    asio::co_spawn(connection->Strand, [=]() -> asio::awaitable<void> {
                        Connection = connection;
                        co_await connection->AsyncRead(connection);
                        Connection.reset();
                    }, asio::detached);
                    co_return connection;
                }
            }
            catch (const std::exception& e) {
                std::cout << "exception: " << e.what() << std::endl;
            }
            co_return nullptr;
        }

    protected:
        asio::strand<asio::io_context::executor_type> Strand;
        std::shared_ptr<FTcpConnection> Connection;
        std::weak_ptr<FTcpConnection> ConnectionWeakPtr;

    };


    class FRpcDispatcher {
    public:
        FRpcDispatcher() {

        }

        template<typename Func>
        bool AddFunc(std::string name, Func&& func) {
            using FuncArgs = boost::callable_traits::args_t<Func>;
            auto f = [func = std::forward<Func>(func)](json::value& args) {
                std::apply(func, json::value_to<FuncArgs>(args));
                };
            return FuncMap.insert(std::make_pair(name, f)).second;
        }

        std::unordered_map<std::string, std::function<void(json::value&)>> FuncMap;
    };

    class FRpcClient : public FTcpClient {
    public:
        FRpcClient(asio::io_context& ioContext)
            : FTcpClient(ioContext)
        {}

        ~FRpcClient() {}

        void OnRecvData(FTcpConnection* connection, const char* data, std::size_t size)
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
            BOOST_ASSERT(Strand.running_in_this_thread());
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


    class FRpcConnection : public FTcpConnection {
    public:
        FRpcConnection(ITcpContext* tcpContext)
            : FTcpConnection(tcpContext)
        {}

        FRpcConnection(ITcpContext* tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
            : FTcpConnection(tcpContext, strand, endpoint)
        {}

        ~FRpcConnection() { }
      
        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection) override
        {
            BOOST_ASSERT(Strand.running_in_this_thread());
            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: connected" << std::endl;
            TcpContext->OnConnected(connection.get());
            try
            {
                char buffer[4 * 1024];
                for (;;)
                {
                    uint32_t bufferSize;
                    auto bytesTransferred = co_await Socket.async_read_some(asio::buffer(&bufferSize, sizeof(bufferSize)), asio::use_awaitable);
                    std::vector<char> buffer;
                    buffer.resize(EndianCast(bufferSize));
                    bytesTransferred = co_await Socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
                    printf("client: ");
                    for (size_t i = 0; i < bytesTransferred; i++)
                    {
                        printf("%c", std::isprint(buffer[i]) ? buffer[i] : '?');
                    }
                    printf("\n");
                    TcpContext->OnRecvData(connection.get(), buffer.data(), buffer.size());
                }
            }
            catch (const std::exception& e)
            {
                Socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
            std::cout << "conn[" << Endpoint.address().to_string() << ":" << Endpoint.port() << "]: disconnected" << std::endl;
            TcpContext->OnConnected(connection.get());
        }

    };

}


//namespace Cpp {
//
//    class FTcpConnection;
//
//    using namespace boost;
//    using FConnectionId = std::pair<asio::ip::address_v4::uint_type, asio::ip::port_type>;
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
//    class ITcpContext {
//    public:
//        ITcpContext(asio::io_context& ioContext)
//            : IoContext(ioContext)
//            , Strand(asio::make_strand(ioContext))
//        {}
//
//        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection) = 0;
//        virtual asio::awaitable<void> AsyncWrite(std::shared_ptr<FTcpConnection> connection, std::vector<uint8_t> data) = 0;
//        virtual asio::awaitable<void> AsyncAccept(std::shared_ptr<asio::ip::tcp::acceptor> acceptor) = 0;
//        virtual bool AcquireConnection(FTcpConnection* connection) = 0;
//        virtual void ReleaseConnection(FTcpConnection* connection) = 0;
//
//        asio::io_context& GetIoContextRef() { return IoContext; }
//
//    protected:
//        asio::io_context& IoContext;
//        asio::strand<asio::io_context::executor_type> Strand;
//
//    };
//
//    class FTcpConnection : public std::enable_shared_from_this<FTcpConnection> 
//    {
//    public:
//        FTcpConnection(ITcpContext& tcpContext, asio::ip::tcp::endpoint endpoint = asio::ip::tcp::endpoint())
//            : TcpContext(tcpContext)
//            , Strand(asio::make_strand(tcpContext.GetIoContextRef()))
//            , Socket(Strand)
//            , Endpoint(endpoint)
//            , bIsConnected(false)
//        {}
//        ~FTcpConnection() {
//            TcpContext.ReleaseConnection(this);
//        }
//
//        void Read() { asio::co_spawn(Strand, TcpContext.AsyncRead(shared_from_this()), asio::detached); }
//        void Write(std::vector<uint8_t> data) { asio::co_spawn(Strand, TcpContext.AsyncWrite(shared_from_this(), std::move(data)), asio::detached); }
//        void Close() { asio::co_spawn(Strand, AsyncClose(shared_from_this()), asio::detached); }
//
//        asio::ip::tcp::socket& GetSocketRef() { return Socket; }
//        asio::strand<asio::io_context::executor_type>& GetStrandRef() { return Strand; }
//        asio::ip::tcp::endpoint& GetEndpointRef() { return Endpoint; }
//        FConnectionId GetId() { return std::make_pair(Endpoint.address().to_v4().to_uint(), Endpoint.port()); }
//        std::queue<std::vector<uint8_t>>& GetWriteQueueRef() { return WriteQueue; }
//        bool IsConnected() { return bIsConnected; }
//
//    protected:
//        asio::awaitable<void> AsyncClose(std::shared_ptr<FTcpConnection> self)
//        {
//            Socket.close();
//            co_return;
//        }
//
//    protected:
//        ITcpContext& TcpContext;
//        asio::strand<asio::io_context::executor_type> Strand;
//        asio::ip::tcp::socket Socket;
//        asio::ip::tcp::endpoint Endpoint;
//        bool bIsConnected;
//        std::queue<std::vector<uint8_t>> WriteQueue;
//
//        friend class FTcpContext;
//    };
//
//    class FTcpContext: public ITcpContext, public std::enable_shared_from_this<FTcpContext> {
//    public:
//
//        FTcpContext(asio::io_context& ioContext)
//            : ITcpContext(ioContext)
//        {}
//        ~FTcpContext() {
//            Stop();
//        }
//
//        void Listen(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
//        {
//            auto acceptor = std::make_shared<asio::ip::tcp::acceptor>(Strand, asio::ip::tcp::endpoint(address, port));
//            Acceptor = acceptor;
//            asio::co_spawn(Strand, AsyncAccept(acceptor), asio::detached);
//        }
//
//        std::future<std::shared_ptr<FTcpConnection>> Connect(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
//        {
//            return asio::co_spawn(Strand, AsyncConnect(shared_from_this()), asio::use_future);
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
//                for (auto& [id, connection] : ConnectionMap)
//                {
//                    if (auto Socket = connection.lock())
//                    {
//                        Socket->Close();
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
//        asio::awaitable<std::shared_ptr<FTcpConnection>> AsyncConnect(std::shared_ptr<FTcpContext> self, std::string address = "127.0.0.1", asio::ip::port_type port = 7772)
//        {
//            try {
//                std::shared_ptr<FTcpConnection> newConnection = std::make_shared<FTcpConnection>(*this, asio::ip::tcp::endpoint(asio::ip::make_address(address), port));
//                auto& socket = newConnection->GetSocketRef();
//                auto& strand = newConnection->GetStrandRef();
//                auto& endpoint = newConnection->GetEndpointRef();
//                co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
//                if (AcquireConnection(newConnection.get()))
//                {
//                    asio::deadline_timer deadlineTimer(strand);
//                    deadlineTimer.expires_from_now(boost::posix_time::seconds(3));
//                    deadlineTimer.async_wait([&](boost::system::error_code errorCode) { if (!errorCode) socket.close(); });
//                    co_await socket.async_connect(endpoint, asio::use_awaitable);
//                    deadlineTimer.cancel();
//                    if (socket.is_open()) {
//                        newConnection->Read();
//                        co_return newConnection;
//                    }
//                }
//            }
//            catch (const std::exception& e) {
//                std::cout << "exception: " << e.what() << std::endl;
//            }
//            co_return nullptr;
//        }
//
//        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpConnection> connection) override
//        {
//            asio::strand<asio::io_context::executor_type>& strand = connection->GetStrandRef();
//            asio::ip::tcp::socket& socket = connection->GetSocketRef();
//            BOOST_ASSERT(strand.running_in_this_thread());
//            char buffer[4 * 1024];
//            auto& endpoint = connection->GetEndpointRef();
//            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: connected" << std::endl;
//            connection->bIsConnected = true;
//            if (!connection->WriteQueue.empty())
//            {
//                asio::co_spawn(strand, AsyncWrite(connection), asio::detached);
//            }
//            try
//            {
//                for (;;)
//                {
//                    auto bytesTransferred = co_await socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
//                    connection->Write(std::vector<uint8_t>(buffer, buffer + bytesTransferred));
//                }
//            }
//            catch (const std::exception& e)
//            {
//                socket.close();
//                std::cout << "exception: " << e.what() << std::endl;
//            }
//            connection->bIsConnected = false;
//            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: disconnected" << std::endl;
//            BOOST_ASSERT(strand.running_in_this_thread());
//        }
//
//        asio::awaitable<void> AsyncWrite(std::shared_ptr<FTcpConnection> connection)
//        {
//            asio::strand<asio::io_context::executor_type>& strand = connection->GetStrandRef();
//            asio::ip::tcp::socket& socket = connection->GetSocketRef();
//            std::queue<std::vector<uint8_t>>& writeQueue = connection->GetWriteQueueRef();
//            try
//            {
//                BOOST_ASSERT(strand.running_in_this_thread());
//                while (!writeQueue.empty())
//                {
//                    auto bytesTransferred = co_await socket.async_write_some(asio::buffer(writeQueue.front()), asio::use_awaitable);
//                    writeQueue.pop();
//                }
//                BOOST_ASSERT(strand.running_in_this_thread());
//            }
//            catch (const std::exception& e)
//            {
//                socket.close();
//                std::cout << "exception: " << e.what() << std::endl;
//            }
//        }
//
//        virtual asio::awaitable<void> AsyncWrite(std::shared_ptr<FTcpConnection> connection, std::vector<uint8_t> data) override
//        {
//            asio::strand<asio::io_context::executor_type>& strand = connection->GetStrandRef();
//            asio::ip::tcp::socket& socket = connection->GetSocketRef();
//            std::queue<std::vector<uint8_t>>& writeQueue = connection->GetWriteQueueRef();
//            try
//            {
//                BOOST_ASSERT(strand.running_in_this_thread());
//                bool bIsWriteQueueEmpty = writeQueue.empty();
//                writeQueue.push(std::move(data));
//                if (!bIsWriteQueueEmpty)
//                    co_return;
//                if (!connection->IsConnected())
//                    co_return;
//                while (!writeQueue.empty())
//                {
//                    auto bytesTransferred = co_await socket.async_write_some(asio::buffer(writeQueue.front()), asio::use_awaitable);
//                    writeQueue.pop();
//                }
//                BOOST_ASSERT(strand.running_in_this_thread());
//            }
//            catch (const std::exception& e)
//            {
//                socket.close();
//                std::cout << "exception: " << e.what() << std::endl;
//            }
//        }
//
//    protected:
//        virtual asio::awaitable<void> AsyncAccept(std::shared_ptr<asio::ip::tcp::acceptor> acceptor) override {
//            try
//            {
//                for (;;)
//                {
//                    std::shared_ptr<FTcpConnection> newConnection = std::make_shared<FTcpConnection>(*this);
//                    auto& socket = newConnection->GetSocketRef();
//                    auto& endpoint = newConnection->GetEndpointRef();
//                    asio::strand<asio::io_context::executor_type> SocketStrand = asio::make_strand(IoContext);
//                    co_await acceptor->async_accept(socket, endpoint, asio::use_awaitable);
//                    if (!endpoint.address().is_v4())
//                    {
//                        continue;
//                    }
//                    co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
//                    if (AcquireConnection(newConnection.get()))
//                    {
//                        newConnection->Read();
//                    }
//                }
//            }
//            catch (const std::exception& e)
//            {
//                std::cout << "exception: " << e.what() << std::endl;
//            }
//        }
//
//        virtual bool AcquireConnection(FTcpConnection* connection) override
//        {
//            BOOST_ASSERT(Strand.running_in_this_thread());
//            auto& endpoint = connection->GetEndpointRef();
//            auto insertResult = ConnectionMap.insert(std::make_pair(std::make_pair(endpoint.address().to_v4().to_uint(), endpoint.port()), connection->shared_from_this()));
//            if (!insertResult.second)
//            {
//                return false;
//            }
//            return true;
//        }
//
//        virtual void ReleaseConnection(FTcpConnection* connection) override
//        {
//            asio::co_spawn(Strand, [this](FConnectionId connectionId) -> asio::awaitable<void> {
//                BOOST_ASSERT(Strand.running_in_this_thread());
//                auto it = ConnectionMap.find(connectionId);
//                if (it != ConnectionMap.end())
//                {
//                    ConnectionMap.erase(it);
//                }
//                co_return;
//            } (connection->GetId()), asio::detached);
//        }
//
//    protected:
//        std::weak_ptr<asio::ip::tcp::acceptor> Acceptor;
//        std::vector<std::shared_ptr<FTcpConnection>> Connections;
//        std::map<FConnectionId, std::weak_ptr<FTcpConnection>> ConnectionMap;
//
//    };
//
//
//}