#pragma once
#include "TcpClient.h"
#include "TcpServer.h"
#include "RpcServer.h"
#include "RpcClient.h"

using namespace Cpp;

asio::io_context ioc;
//std::shared_ptr<FRpcClient> rpcClient = std::make_shared<FRpcClient>(ioc);
std::shared_ptr<FTcpClient> tcpClient = std::make_shared<FTcpClient>(ioc);


void testTcp() {
    tcpClient->GetTcpContext()->HeartbeatData = { 'r', 'p', 'c', };
    tcpClient->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
    tcpClient->GetTcpContext()->ConnectedFunc = [](FTcpConnection* connection) {
        std::cout << "client connected " << std::endl;;
    };
    tcpClient->GetTcpContext()->DisconnectedFunc = [](FTcpConnection* connection) {
        std::cout << "client disconnectd " << std::endl;
    };
    tcpClient->GetTcpContext()->RecvDataFunc = [](FTcpConnection* connection, const char* data, std::size_t size) {
        printf("server: ");
        for (size_t i = 0; i < size; i++)
        {
            printf("%c", data[i]);
        }
        printf("\n");
    };
    tcpClient->Start();
}

//asio::awaitable<void> test() {
//    rpcClient->SetAttachedFunc([]() { 
//        rpcClient->Call("exec", []() -> asio::awaitable<void> {
//            std::cout << "server exec < " << std::endl;
//            co_return;
//        }, "print(\"client\")");
//    });
//    rpcClient->SetDetachedFunc([]() {
//        std::cout << "client disconnectd " << std::endl;
//    });
//    auto tcpSocket = co_await rpcClient->AsyncConnect(asio::ip::make_address("127.0.0.1"), 7772);
//}

int main(int argc, char* argv[]) {

    std::unique_ptr<asio::io_context::work> work = std::make_unique<asio::io_context::work>(ioc);
    std::thread t([&] {
        ioc.run();
    });
    {
        //rpcClient->SetLogFunc([](const char* msg) {
        //    std::cout << msg << std::endl;
        //    });
        //tcpClient->SetLogFunc([](const char* msg) {
        //    std::cout << msg << std::endl;
        //    });
        //rpcClient->RpcDispatcher.AddFunc("exec", [=](std::string cmd) -> asio::awaitable<int> {
        //    std::cout << "client exec > " << cmd << std::endl;
        //    co_return 7787;
        //});
        //asio::co_spawn(ioc, test(), asio::detached);
        //std::shared_ptr<FRpcServer> tcpServer = std::make_shared<FRpcServer>(ioc);
        //tcpServer->SetLogFunc([](const char* msg) {
        //    std::cout << msg << std::endl;
        //});
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
        std::shared_ptr<FTcpServer> tcpServer = std::make_shared<FTcpServer>(ioc);
        tcpServer->GetTcpContext()->HeartbeatData = { 'r', 'p', 'c', };
        tcpServer->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
        tcpServer->GetTcpContext()->ConnectedFunc = std::bind(&FTcpServer::OnConnected, std::weak_ptr(tcpServer), std::placeholders::_1);
        tcpServer->GetTcpContext()->DisconnectedFunc = std::bind(&FTcpServer::OnDisconnected, std::weak_ptr(tcpServer), std::placeholders::_1);
        tcpServer->GetTcpContext()->RecvDataFunc = [](FTcpConnection* connection, const char* data, std::size_t size) {
            printf("client: ");
            for (size_t i = 0; i < size; i++)
            {
                printf("%c", data[i]);
            }
            printf("\n");
        };
        tcpServer->Start();
        testTcp();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        //rpcClient.reset();
        tcpClient.reset();
    }
    work.reset();
    t.join();
    return 0;
}
