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

    tcpClient->OnConnectedFunc = [](FTcpConnection* connection) { std::cout << "client connected " << std::endl;; };
    tcpClient->OnDisconnectedFunc = [](FTcpConnection* connection) { 
        std::cout << "client disconnectd " << std::endl;
        };
    auto tcpSocket = co_await tcpClient->AsyncConnect(asio::ip::make_address("127.0.0.1"), 7772);
    tcpClient->Call("aaa", [](int v) -> asio::awaitable<void> { std::cout << "client resp " << std::endl; co_return; }, "bbb", "ccc");

}

int main(int argc, char* argv[]) {

    std::unique_ptr<asio::io_context::work> work = std::make_unique<asio::io_context::work>(ioc);
    std::thread t([&] {
        ioc.run();
    });
    {
        tcpClient->RpcDispatcher.AddFunc("ddd", [=](std::string a, std::string b) -> asio::awaitable<int> {
            std::cout << "client call ddd(" << a << "," << b << ")" << std::endl;
            co_return 7787;
            });
        asio::co_spawn(ioc, test(), asio::detached);
        std::shared_ptr<FRpcServer> tcpServer = std::make_shared<FRpcServer>(ioc);
        tcpServer->OnConnectedFunc = [](FTcpConnection* connection) { 
            std::cout << "server connected " << std::endl; 
        };
        tcpServer->OnDisconnectedFunc = [](FTcpConnection* connection) { 
            std::cout << "server disconnected " << std::endl;; 
        };
        tcpServer->RpcDispatcher.AddFunc("aaa", [tcpServer = tcpServer.get()](std::string a, std::string b) -> asio::awaitable<int> {
            std::cout << "server call aaa(" << a << "," << b << ")" << std::endl;
            tcpServer->Call(asio::ip::make_address_v4("127.0.0.1"), "ddd", [] {
                std::cout << "server resp " << std::endl;
            }, "eee", "fff");
            co_return 7787;
        });
        tcpServer->Start();
        std::this_thread::sleep_for(std::chrono::seconds(32));
        tcpClient.reset();
    }
    work.reset();
    t.join();
    return 0;
}

