#pragma once
#include "TcpClient.h"
#include "TcpServer.h"
#include "RpcServer.h"
#include "RpcClient.h"

using namespace Cpp;

int main(int argc, char* argv[]) {

    asio::io_context ioc;
    std::unique_ptr<asio::io_context::work> work = std::make_unique<asio::io_context::work>(ioc);
    std::thread t([&] {
        ioc.run();
    });
    {
        std::shared_ptr<FRpcServer> rpcServer = std::make_shared<FRpcServer>(ioc);
        rpcServer->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
        //rpcServer->GetTcpContext()->ConnectedFunc = std::bind(&FTcpServer::OnConnected, std::weak_ptr(rpcServer), std::placeholders::_1);
        //rpcServer->GetTcpContext()->DisconnectedFunc = std::bind(&FTcpServer::OnDisconnected, std::weak_ptr(rpcServer), std::placeholders::_1);
        rpcServer->RefRpcDispatcher().AddFunc("exec", [rpcServer = rpcServer.get()](std::string cmd) -> asio::awaitable<int> {
            std::cout << "server exec > " << cmd << std::endl;
            rpcServer->Call(asio::ip::make_address_v4("127.0.0.1"), "exec", [] {
                std::cout << "client exec < " << std::endl;
            }, "print(\"server\")");
            co_return 7787;
        });
        rpcServer->Start();

        std::shared_ptr<FRpcClient> rpcClient = std::make_shared<FRpcClient>(ioc);
        rpcClient->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
        rpcClient->RefRpcDispatcher().AddFunc("exec", [rpcServer = rpcServer.get()](std::string cmd) -> asio::awaitable<void> {
            std::cout << "client exec > " << cmd << std::endl;
            co_return;
        });
        rpcClient->SetAttachedFunc([rpcClient = rpcClient.get()]() {
            rpcClient->Call("exec", []() -> asio::awaitable<void> {
                std::cout << "server exec < " << std::endl;
                co_return;
            }, "print(\"client\")");
        });
        rpcClient->SetDetachedFunc([]() {
            std::cout << "client disconnectd " << std::endl;
        });
        rpcClient->Start();

        std::shared_ptr<FTcpServer> tcpServer = std::make_shared<FTcpServer>(ioc);
        tcpServer->GetTcpContext()->HeartbeatData = { 'r', 'p', 'c', };
        //tcpServer->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
        //tcpServer->GetTcpContext()->ConnectedFunc = std::bind(&FTcpServer::OnConnected, std::weak_ptr(tcpServer), std::placeholders::_1);
        //tcpServer->GetTcpContext()->DisconnectedFunc = std::bind(&FTcpServer::OnDisconnected, std::weak_ptr(tcpServer), std::placeholders::_1);
        tcpServer->GetTcpContext()->RecvDataFunc = [](FTcpConnection* connection, const char* data, std::size_t size) {
            printf("client: ");
            for (size_t i = 0; i < size; i++)
            {
                printf("%c", data[i]);
            }
            printf("\n");
        };
        tcpServer->Start(asio::ip::address_v4::any(), 7777);

        std::shared_ptr<FTcpClient> tcpClient = std::make_shared<FTcpClient>(ioc);
        tcpClient->GetTcpContext()->HeartbeatData = { 'r', 'p', 'c', };
        //tcpClient->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
        tcpClient->GetTcpContext()->ConnectedFunc = [](FTcpConnection* connection) { std::cout << "ConnectedFunc" << std::endl; };
        tcpClient->GetTcpContext()->DisconnectedFunc = [](FTcpConnection* connection) { std::cout << "DisconnectedFunc" << std::endl; };
        tcpClient->GetTcpContext()->RecvDataFunc = [](FTcpConnection* connection, const char* data, std::size_t size) {
            printf("server: ");
            for (size_t i = 0; i < size; i++)
            {
                printf("%c", data[i]);
            }
            printf("\n");
        };
        tcpClient->Start(asio::ip::address_v4::loopback(), 7777);
        tcpClient->Start(asio::ip::make_address_v4("192.168.1.111"), 7777);
        tcpClient->Start(asio::ip::address_v4::loopback(), 7777);
        tcpClient->Start(asio::ip::make_address_v4("192.168.1.111"), 7777);
        tcpClient->Start(asio::ip::address_v4::loopback(), 7777);
        tcpClient->Start(asio::ip::make_address_v4("192.168.1.111"), 7777);
        tcpClient->Start(asio::ip::address_v4::loopback(), 7777);
        tcpClient->Start(asio::ip::make_address_v4("192.168.1.111"), 7777);
        tcpClient->Start(asio::ip::address_v4::loopback(), 7777);

        std::this_thread::sleep_for(std::chrono::seconds(16));
    }
    work.reset();
    t.join();
    return 0;
}
