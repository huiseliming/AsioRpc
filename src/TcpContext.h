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

namespace Private {

    using namespace boost;
    using FSocketId = std::pair<asio::ip::address_v4::uint_type, asio::ip::port_type>;

    class FTcpSocket;

    template <typename T>
    inline T EndianCast(const T& val)
    {
        if constexpr (std::endian::native == std::endian::little) {
            return boost::endian::endian_reverse(val);
        }
        return val;
    }

    class ITcpContext {
    public:
        ITcpContext(asio::io_context& ioContext)
            : IoContext(ioContext)
        {}

        virtual asio::awaitable<void> AsyncRead(std::shared_ptr<FTcpSocket> tcpSocket) = 0;
        virtual asio::awaitable<void> AsyncWrite(std::shared_ptr<FTcpSocket> tcpSocket, std::vector<uint8_t> data) = 0;
        virtual bool AcquireSocket(FTcpSocket* tcpSocket) = 0;
        virtual void ReleaseSocket(FTcpSocket* tcpSocket) = 0;

        asio::io_context& IoContextRef() { return IoContext; }

    protected:
        asio::io_context& IoContext;

    };

    class FTcpSocket : public std::enable_shared_from_this<FTcpSocket>
    {
    public:
        FTcpSocket(ITcpContext& tcpContext)
            : TcpContext(tcpContext)
            , Strand(asio::make_strand(tcpContext.IoContextRef()))
            , Socket(Strand)
        {}

        FTcpSocket(ITcpContext& tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
            : TcpContext(tcpContext)
            , Strand(strand)
            , Socket(Strand)
            , Endpoint(endpoint)
        {}

        ~FTcpSocket() {
            TcpContext.ReleaseSocket(this);
        }

        void Read() { asio::co_spawn(Strand, TcpContext.AsyncRead(shared_from_this()), asio::detached); }
        void Write(std::vector<uint8_t> data) { asio::co_spawn(Strand, TcpContext.AsyncWrite(shared_from_this(), std::move(data)), asio::detached); }
        void Close() { asio::co_spawn(Strand, AsyncClose(shared_from_this()), asio::detached); }

        asio::ip::tcp::socket& GetSocketRef() { return Socket; }
        asio::strand<asio::io_context::executor_type>& GetStrandRef() { return Strand; }
        asio::ip::tcp::endpoint& GetEndpointRef() { return Endpoint; }
        FSocketId GetId() { return std::make_pair(Endpoint.address().to_v4().to_uint(), Endpoint.port()); }
        std::queue<std::vector<uint8_t>>& GetWriteQueueRef() { return WriteQueue; }

    protected:
        asio::awaitable<void> AsyncClose(std::shared_ptr<FTcpSocket> self)
        {
            co_return Socket.close();
        }

    protected:
        ITcpContext& TcpContext;
        asio::strand<asio::io_context::executor_type> Strand;
        asio::ip::tcp::socket Socket;
        asio::ip::tcp::endpoint Endpoint;
        std::queue<std::vector<uint8_t>> WriteQueue;

    };

    class FRpcDispatcher {
    public:
        FRpcDispatcher() {
        
        }

        template<typename Func>
        bool AddFunc(std::string name, Func&& func) {
            return FuncMap.insert(std::make_pair(name, [func = std::move(func)](json::array& args) { ; })).second;
        }

        std::unordered_map<std::string, std::function<void(json::array&)>> FuncMap;
    };


    class FRpcSocket : public FTcpSocket {
    public:
        FRpcSocket(ITcpContext& tcpContext)
            : FTcpSocket(tcpContext)
        {}

        FRpcSocket(ITcpContext& tcpContext, asio::strand<asio::io_context::executor_type> strand, asio::ip::tcp::endpoint endpoint)
            : FTcpSocket(tcpContext, strand, endpoint)
        {}

        ~FRpcSocket() { }

        template<typename ... Args>
        asio::awaitable<void> AsyncCall(int64_t requestId, std::string func, std::tuple<Args...> args) {
            std::string callableString = json::serialize(json::value_from(std::make_tuple(requestId, func, args)));
            uint32_t bufferSize = callableString.size();
            std::vector<uint8_t> buffer;
            buffer.resize(sizeof(uint32_t) + bufferSize);
            *reinterpret_cast<uint32_t*>(buffer.data()) = EndianCast(bufferSize);
            std::memcpy(buffer.data() + sizeof(uint32_t), callableString.data(), bufferSize);
            Write(buffer);
            co_return;
        }

        template<typename ... Args>
        void Call(int64_t requestId, std::string func, Args&& ... args) {
            asio::co_spawn(Strand, AsyncCall(requestId, std::move(func), std::make_tuple(args...)), asio::detached);
        }

    };

}