#pragma once
#include "../AsioRpc.h"

#include "TcpClient.h"
#include "TcpServer.h"
#include "RpcServer.h"
#include "RpcClient.h"

using namespace Cpp;

asio::io_context ioc;
std::shared_ptr<FRpcClient> rpcClient = std::make_shared<FRpcClient>(ioc);
std::shared_ptr<FTcpClient> tcpClient = std::make_shared<FTcpClient>(ioc);

asio::awaitable<void> testTcp() {

    tcpClient->SetConnectedFunc([](FTcpConnection* connection) { 
        std::cout << "client connected " << std::endl;;
    });
    tcpClient->SetDisconnectedFunc([](FTcpConnection* connection) {
        std::cout << "client disconnectd " << std::endl;
    });
    tcpClient->SetRecvDataFunc([](FTcpConnection* connection, const char* data, std::size_t size) {
        printf("client: ");
        for (size_t i = 0; i < size; i++)
        {
            printf("%c", data[i]);
        }
        printf("\n");
    });
    auto tcpSocket = co_await tcpClient->AsyncConnect(asio::ip::make_address("127.0.0.1"), 7777);

    co_return;
}

asio::awaitable<void> test() {

    rpcClient->SetConnectedFunc([](FTcpConnection* connection) { std::cout << "client connected " << std::endl;; });
    rpcClient->SetDisconnectedFunc([](FTcpConnection* connection) {
        std::cout << "client disconnectd " << std::endl;
    });
    auto tcpSocket = co_await rpcClient->AsyncConnect(asio::ip::make_address("127.0.0.1"), 7772);
    rpcClient->Call("exec", []() -> asio::awaitable<void> {
        std::cout << "server exec < " << std::endl;
        co_return;
    }, "print(\"client\")");
}

int main(int argc, char* argv[]) {

    std::unique_ptr<asio::io_context::work> work = std::make_unique<asio::io_context::work>(ioc);
    std::thread t([&] {
        ioc.run();
    });
    {
        asio::co_spawn(ioc, testTcp(), asio::detached);
        
        //rpcClient->RpcDispatcher.AddFunc("exec", [=](std::string cmd) -> asio::awaitable<int> {
        //    std::cout << "client exec > " << cmd << std::endl;
        //    co_return 7787;
        //});
        //asio::co_spawn(ioc, test(), asio::detached);
        //std::shared_ptr<FRpcServer> tcpServer = std::make_shared<FRpcServer>(ioc);
        //tcpServer->SetConnectedFunc([](FTcpConnection* connection) {
        //    std::cout << "server connected " << std::endl; 
        //});
        //tcpServer->SetDisconnectedFunc([](FTcpConnection* connection) {
        //    std::cout << "server disconnected " << std::endl;; 
        //});
        //tcpServer->RpcDispatcher.AddFunc("exec", [tcpServer = tcpServer.get()](std::string cmd) -> asio::awaitable<int> {
        //    std::cout << "server exec > " << cmd << std::endl;
        //    tcpServer->Call(asio::ip::make_address_v4("127.0.0.1"), "exec", [] {
        //        std::cout << "client exec < " << std::endl;
        //    }, "print(\"server\")");
        //    co_return 7787;
        //});
        //tcpServer->Start();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        rpcClient.reset();
        tcpClient.reset();
    }
    work.reset();
    t.join();
    return 0;
}
