#pragma once
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <queue>
#include <iostream>
#include <functional>
#include <unordered_map>
#include <fmt/format.h>
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

    class ITcpContext : public std::enable_shared_from_this<ITcpContext>
    {
    public:
        ITcpContext(asio::io_context& ioContext)
            : IoContext(ioContext)
        {}
        virtual ~ITcpContext() {}

        BOOST_FORCEINLINE void Log(const char* msg) {
            if (LogFunc) LogFunc(msg);
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

    //protected:
        asio::io_context& IoContext;
        double OperationTimeout = 3.f;
        std::vector<uint8_t> HeartbeatData;
        std::function<void(const char*)> LogFunc;
        std::function<void(FTcpConnection*)> ConnectedFunc;
        std::function<void(FTcpConnection*)> DisconnectedFunc;
        std::function<void(FTcpConnection*, const char*, std::size_t)> RecvDataFunc;
        
    };


}