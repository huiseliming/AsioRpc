#pragma once
#include "TcpContext.h"


namespace Private {

    template<typename T = FTcpSocket>
    class TTcpClient : public ITcpContext {
    public:

        TTcpClient(asio::io_context& ioContext)
            : ITcpContext(ioContext)
            , Strand(asio::make_strand(ioContext))
        {}

        ~TTcpClient() {
            Stop();
        }

        std::future<std::shared_ptr<FTcpSocket>> Start(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            return asio::co_spawn(Strand, AsyncStart(address, port), asio::use_future);
        }

        asio::awaitable<std::shared_ptr<FTcpSocket>> AsyncStart(asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            co_await AsyncStop(SocketWeakPtr);
            std::shared_ptr<T> tcpSocket = std::make_shared<T>(*this, Strand, asio::ip::tcp::endpoint(address, port));
            SocketWeakPtr = tcpSocket;
            co_return co_await AsyncConnect(std::move(tcpSocket));
        }

        void Stop()
        {
            asio::co_spawn(Strand, AsyncStop(SocketWeakPtr), asio::use_future).get();
        }

        asio::awaitable<void> AsyncStop(std::weak_ptr<FTcpSocket> tcpSocketWeakPtr)
        {
            if (!Strand.running_in_this_thread()) {
                co_await asio::dispatch(asio::bind_executor(Strand, asio::use_awaitable));
            }
            if (auto tcpSocket = tcpSocketWeakPtr.lock()) {
                tcpSocket->Close();
            }
            asio::steady_timer timer(Strand);
            while (!tcpSocketWeakPtr.expired()) {
                timer.expires_after(std::chrono::milliseconds(1));
                co_await timer.async_wait(asio::use_awaitable);
            }
        }

        asio::awaitable<std::shared_ptr<FTcpSocket>> AsyncConnect(std::shared_ptr<FTcpSocket> tcpSocket, asio::ip::address address = asio::ip::address_v4::any(), asio::ip::port_type port = 7772)
        {
            try {
                auto& socket = tcpSocket->GetSocketRef();
                auto& strand = tcpSocket->GetStrandRef();
                auto& endpoint = tcpSocket->GetEndpointRef();
                asio::deadline_timer deadlineTimer(strand);
                deadlineTimer.expires_from_now(boost::posix_time::seconds(3));
                deadlineTimer.async_wait([&](boost::system::error_code errorCode) { if (!errorCode) socket.close(); });
                co_await socket.async_connect(endpoint, asio::use_awaitable);
                deadlineTimer.cancel();
                if (socket.is_open() && AcquireSocket(tcpSocket.get()))
                {
                    tcpSocket->Read();
                    co_return tcpSocket;
                }
            }
            catch (const std::exception& e) {
                std::cout << "exception: " << e.what() << std::endl;
            }
            co_return nullptr;
        }

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpSocket> tcpSocket) override
        {
            asio::strand<asio::io_context::executor_type>& strand = tcpSocket->GetStrandRef();
            asio::ip::tcp::socket& socket = tcpSocket->GetSocketRef();
            BOOST_ASSERT(strand.running_in_this_thread());
            char buffer[4 * 1024];
            auto& endpoint = tcpSocket->GetEndpointRef();
            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: connected" << std::endl;
            try
            {
                for (;;)
                {
                    auto bytesTransferred = co_await socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
                    printf("client: ");
                    for (size_t i = 0; i < bytesTransferred; i++)
                    {
                        printf("%c", std::isprint(buffer[i]) ? buffer[i] : '?');
                    }
                    printf("\n");
                    BOOST_ASSERT(strand.running_in_this_thread());
                    //tcpSocket->Write(std::vector<uint8_t>(buffer, buffer + bytesTransferred));
                }
            }
            catch (const std::exception& e)
            {
                socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: disconnected" << std::endl;
        }

        virtual asio::awaitable<void> AsyncWrite(std::shared_ptr<FTcpSocket> tcpSocket, std::vector<uint8_t> data) override
        {
            asio::strand<asio::io_context::executor_type>& strand = tcpSocket->GetStrandRef();
            asio::ip::tcp::socket& socket = tcpSocket->GetSocketRef();
            std::queue<std::vector<uint8_t>>& writeQueue = tcpSocket->GetWriteQueueRef();
            try
            {
                BOOST_ASSERT(strand.running_in_this_thread());
                bool bIsWriteQueueEmpty = writeQueue.empty();
                writeQueue.push(std::move(data));
                if (!bIsWriteQueueEmpty)
                    co_return;
                while (!writeQueue.empty())
                {
                    auto bytesTransferred = co_await socket.async_write_some(asio::buffer(writeQueue.front()), asio::use_awaitable);
                    writeQueue.pop();
                }
                BOOST_ASSERT(strand.running_in_this_thread());
            }
            catch (const std::exception& e)
            {
                socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
        }

    protected:

        virtual bool AcquireSocket(FTcpSocket* tcpSocket) override
        {
            Socket = std::static_pointer_cast<T>(tcpSocket->shared_from_this());
            return true;
        }

        virtual void ReleaseSocket(FTcpSocket* tcpSocket) override
        {
            Socket.reset();
        }

    protected:
        asio::strand<asio::io_context::executor_type> Strand;
        std::shared_ptr<T> Socket;
        std::weak_ptr<T> SocketWeakPtr;

    };

    class FRpcClient : public TTcpClient<FRpcSocket> {
    public:
        FRpcClient(asio::io_context& ioContext)
            : TTcpClient(ioContext)
        {}

        ~FRpcClient() {}

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpSocket> tcpSocket) override
        {
            asio::strand<asio::io_context::executor_type>& strand = tcpSocket->GetStrandRef();
            asio::ip::tcp::socket& socket = tcpSocket->GetSocketRef();
            BOOST_ASSERT(strand.running_in_this_thread());
            char buffer[4 * 1024];
            auto& endpoint = tcpSocket->GetEndpointRef();
            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: connected" << std::endl;
            try
            {
                for (;;)
                {
                    uint32_t bufferSize;
                    auto bytesTransferred = co_await socket.async_read_some(asio::buffer(&bufferSize, sizeof(bufferSize)), asio::use_awaitable);
                    std::vector<char> buffer;
                    buffer.resize(EndianCast(bufferSize));
                    bytesTransferred = co_await socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
                    printf("client: ");
                    for (size_t i = 0; i < bytesTransferred; i++)
                    {
                        printf("%c", std::isprint(buffer[i]) ? buffer[i] : '?');
                    }
                    printf("\n");
                    json::value callableValue = json::parse(std::string_view(buffer.data(), buffer.size()));
                    auto& callableArgs = callableValue.as_array();
                    BOOST_ASSERT(callableArgs.size() == 3);
                    int64_t requestId = callableArgs[0].get_int64();
                    const char* func = callableArgs[1].get_string().c_str();
                    auto it = RpcDispatcher.FuncMap.find(func);
                    if (it != RpcDispatcher.FuncMap.end()) {
                        it->second(callableArgs[2]);
                    }
                    BOOST_ASSERT(strand.running_in_this_thread());
                    //tcpSocket->Write(std::vector<uint8_t>(buffer, buffer + bytesTransferred));
                }
            }
            catch (const std::exception& e)
            {
                socket.close();
                std::cout << "exception: " << e.what() << std::endl;
            }
            std::cout << "conn[" << endpoint.address().to_string() << ":" << endpoint.port() << "]: disconnected" << std::endl;
        }

        template<typename ... Args>
        void Call(asio::ip::address_v4::uint_type ip, std::string func, Args&& ... args) {
            if (Socket)
            {
                int64_t requestId = RequestCounter.fetch_add(1, std::memory_order_relaxed);
                Socket->Call(requestId, std::move(func), std::forward<Args>(args)...);
                RequestMap.insert(std::pair(requestId, [] {; }));
            }
        }

        FRpcDispatcher RpcDispatcher;
        std::atomic<int64_t> RequestCounter;
        std::unordered_map<int64_t, std::function<void()>> RequestMap;
    };

}