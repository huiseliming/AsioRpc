#pragma once
#include "CmdConnection.h"
#include "TcpClient.h"

namespace Cpp {

    class FCmdClient : public FTcpClient {
    protected:
        struct FImpl : public FTcpClient::FImpl {
        public:
            FImpl(asio::io_context& ioContext)
                : FTcpClient::FImpl(ioContext)
            {}
            virtual std::shared_ptr<FTcpConnection> NewConnection(asio::ip::address address, asio::ip::port_type port) override {
                return std::make_shared<FCmdConnection>(shared_from_this(), Strand, asio::ip::tcp::endpoint(address, port));
            }
        };
    public:
        FCmdClient(asio::io_context& ioContext)
            : FTcpClient(ioContext, std::make_shared<FImpl>(ioContext))
        {}

        ~FCmdClient() { }

    };

}