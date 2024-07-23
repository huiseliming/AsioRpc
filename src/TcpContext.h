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
        virtual ~ITcpContext() {}
        asio::io_context& RefIoContext() { return IoContext; }
        double GetOperationTimeout() { return OperationTimeout; }

        BOOST_FORCEINLINE void SetOperationTimeout(double operationTimeout) {
            OperationTimeout = operationTimeout;
        }
        BOOST_FORCEINLINE void SetLogFunc(std::function<void(const char*)> logFunc) {
            LogFunc = logFunc;
        }
        BOOST_FORCEINLINE void SetConnectedFunc(std::function<void(FTcpConnection*)> connectedFunc) {
            ConnectedFunc = connectedFunc;
        }
        BOOST_FORCEINLINE void SetDisconnectedFunc(std::function<void(FTcpConnection*)> disconnectedFunc) {
            DisconnectedFunc = disconnectedFunc;
        }
        BOOST_FORCEINLINE void SetRecvDataFunc(std::function<void(FTcpConnection*, const char*, std::size_t)> recvDataFunc) {
            RecvDataFunc = recvDataFunc;
        }

        BOOST_FORCEINLINE void OnConnected(FTcpConnection* connection) {
            if (ConnectedFunc) ConnectedFunc(connection);
        }
        BOOST_FORCEINLINE void OnDisconnected(FTcpConnection* connection) {
            if (DisconnectedFunc) DisconnectedFunc(connection);
        }
        BOOST_FORCEINLINE void OnRecvData(FTcpConnection* connection, const char* data, std::size_t size) {
            if (RecvDataFunc) RecvDataFunc(connection, data, size);
        }

    protected:
        asio::io_context& IoContext;
        double OperationTimeout = 3.f;
        std::function<void(const char*)> LogFunc;
        std::function<void(FTcpConnection*)> ConnectedFunc;
        std::function<void(FTcpConnection*)> DisconnectedFunc;
        std::function<void(FTcpConnection*, const char*, std::size_t)> RecvDataFunc;
        
    };

}