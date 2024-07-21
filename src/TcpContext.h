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

namespace Private {

    using namespace boost;
    using FSocketId = std::pair<asio::ip::address_v4::uint_type, asio::ip::port_type>;

    class FTcpSocket;

    class ITcpContext {
    public:
        ITcpContext(asio::io_context& ioContext)
            : IoContext(ioContext)
        {}

        virtual std::shared_ptr<FTcpSocket> NewTcpSocket() = 0;
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

}