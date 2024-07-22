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

        virtual void OnConnected(FTcpConnection* connection) { }
        virtual void OnRecvData(FTcpConnection* connection, const char* data, std::size_t size) { }
        virtual void OnDisconnected(FTcpConnection* connection) { }

    protected:
        asio::io_context& IoContext;

    };

}