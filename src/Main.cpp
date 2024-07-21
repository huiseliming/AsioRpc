#pragma once
#include "../AsioRpc.h"

#include "TcpClient.h"
#include "TcpServer.h"
#include "RpcServer.h"
#include "RpcClient.h"

using namespace Cpp;

asio::io_context ioc;
std::shared_ptr<FRpcClient> tcpClient = std::make_shared<FRpcClient>(ioc);

asio::awaitable<void> test() {
    auto tcpSocket = co_await tcpClient->AsyncConnect(asio::ip::make_address("127.0.0.1"), 7772);
    tcpClient->Call("aaa", [](int v) { std::cout << "test " << v << std::endl; }, "bbb", "ccc");


}

int main(int argc, char* argv[]) {

    std::unique_ptr<asio::io_context::work> work = std::make_unique<asio::io_context::work>(ioc);
    std::thread t([&] {
        ioc.run();
    });
    {
        asio::co_spawn(ioc, test(), asio::detached);
        std::shared_ptr<FRpcServer> tcpServer = std::make_shared<FRpcServer>(ioc);
        tcpServer->RpcDispatcher.AddFunc("aaa", [](std::string a, std::string b) {
            std::cout << "rpc call aaa(" << a << "," << b << ")" << std::endl;
            return 7787;
        });
        tcpServer->Start();
        std::this_thread::sleep_for(std::chrono::seconds(64));
    }
    work.reset();
    t.join();
    return 0;
}

