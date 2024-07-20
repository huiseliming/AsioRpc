#pragma once
//#include "../AsioRpc.h"

#include "TcpClient.h"
#include "TcpServer.h"

using namespace Private;

asio::io_context ioc;
std::shared_ptr<FTcpClient> tcpClient = std::make_shared<FTcpClient>(ioc);

asio::awaitable<void> test() {
    auto tcpSocket = co_await tcpClient->AsyncStart(asio::ip::make_address("127.0.0.1"), 7777);
    tcpSocket->Write(std::vector<uint8_t>{'s', 'b' });
}


int main(int argc, char* argv[]) {

    std::unique_ptr<asio::io_context::work> work = std::make_unique<asio::io_context::work>(ioc);
    std::thread t([&] {
        ioc.run();
    });
    {
        asio::co_spawn(ioc, test(), asio::detached);
        std::shared_ptr<FTcpServer> tcpServer = std::make_shared<FTcpServer>(ioc);
        tcpServer->Start();
        std::this_thread::sleep_for(std::chrono::seconds(64));
    }
    work.reset();
    t.join();
    return 0;
}