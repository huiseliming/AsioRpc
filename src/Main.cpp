#pragma once
#include "TcpClient.h"
#include "TcpServer.h"
#include "RpcServer.h"
#include "RpcClient.h"
#include "CmdClient.h"

using namespace Cpp;

std::shared_ptr<FCmdClient> testCmdClient(asio::io_context& ioc) {

    std::shared_ptr<FCmdClient> cmdClient = std::make_shared<FCmdClient>(ioc);
    cmdClient->GetTcpContext()->HeartbeatData = { 'c', 'm', 'd'};
    //cmdClient->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
    cmdClient->GetTcpContext()->ConnectedFunc = [](FTcpConnection* connection) {
        std::cout << "ConnectedFunc" << std::endl;
        for (size_t i = 0; i < 32; i++)
        {
            connection->Write({ '@' });
        }
        };
    cmdClient->GetTcpContext()->DisconnectedFunc = [](FTcpConnection* connection) { std::cout << "DisconnectedFunc" << std::endl; };
    cmdClient->GetTcpContext()->RecvDataFunc = [](FTcpConnection* connection, std::vector<uint8_t> buffer) {
        printf("server: ");
        for (size_t i = 0; i < buffer.size(); i++)
        {
            printf("%c", buffer[i]);
        }
        printf("\n");
        };
    cmdClient->Start(asio::ip::address_v4::loopback(), 7776);
    return cmdClient;
}

int main(int argc, char* argv[]) {
    asio::io_context ioc;
    std::unique_ptr<asio::io_context::work> work = std::make_unique<asio::io_context::work>(ioc);
    std::thread t([&] { ioc.run(); });
    try
    {
        std::shared_ptr<FRpcServer> rpcServer = std::make_shared<FRpcServer>(ioc);
        rpcServer->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
        rpcServer->RefRpcDispatcher()->AddFunc("exec", [&, rpcDispatcher = rpcServer->RefRpcDispatcher()](std::string cmd) -> asio::awaitable<int> {
            std::cout << "server exec > " << cmd << std::endl;
            rpcDispatcher->Call("127.0.0.1", "exec", [] {
                std::cout << "client exec < " << std::endl;
            }, "print(\"server\")");
            co_return 7787;
        });
        rpcServer->Start();

        std::shared_ptr<FRpcClient> rpcClient = std::make_shared<FRpcClient>(ioc);
        rpcClient->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
        rpcClient->RefRpcDispatcher()->AddFunc("exec", [rpcServer = rpcServer.get()](std::string cmd) -> asio::awaitable<void> {
            std::cout << "client exec > " << cmd << std::endl;
            co_return;
        });
        rpcClient->RefRpcDispatcher()->SetAttachedFunc([rpcDispatcher = rpcClient->RefRpcDispatcher()](FTcpConnection* connection) {
            rpcDispatcher->Call(std::shared_ptr<FTcpConnection>(), "exec", []() -> asio::awaitable<void> {
                std::cout << "server exec < " << std::endl;
                co_return;
            }, "print(\"client\")");
        });
        rpcClient->Start();

        std::shared_ptr<FTcpServer> tcpServer = std::make_shared<FTcpServer>(ioc);
        tcpServer->GetTcpContext()->HeartbeatData = { 't', 'c', 'p', };
        //tcpServer->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
        //tcpServer->GetTcpContext()->ConnectedFunc = std::bind(&FTcpServer::OnConnected, std::weak_ptr(tcpServer), std::placeholders::_1);
        //tcpServer->GetTcpContext()->DisconnectedFunc = std::bind(&FTcpServer::OnDisconnected, std::weak_ptr(tcpServer), std::placeholders::_1);
        tcpServer->GetTcpContext()->RecvDataFunc = [](FTcpConnection* connection, std::vector<uint8_t> buffer) {
            printf("client: ");
            for (size_t i = 0; i < buffer.size(); i++)
            {
                printf("%c", buffer[i]);
            }
            printf("\n");
        };
        tcpServer->Start(asio::ip::address_v4::any(), 7777);

        std::shared_ptr<FTcpClient> tcpClient = std::make_shared<FTcpClient>(ioc);
        tcpClient->GetTcpContext()->HeartbeatData = { 't', 'c', 'p', };
        //tcpClient->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
        tcpClient->GetTcpContext()->ConnectedFunc = [](FTcpConnection* connection) { std::cout << "ConnectedFunc" << std::endl; };
        tcpClient->GetTcpContext()->DisconnectedFunc = [](FTcpConnection* connection) { std::cout << "DisconnectedFunc" << std::endl; };
        tcpClient->GetTcpContext()->RecvDataFunc = [](FTcpConnection* connection, std::vector<uint8_t> buffer) {
            printf("server: ");
            for (size_t i = 0; i < buffer.size(); i++)
            {
                printf("%c", buffer[i]);
            }
            printf("\n");
        };
        tcpClient->Start(asio::ip::address_v4::loopback(), 7777);

        auto cmdClient = testCmdClient(ioc);

        std::this_thread::sleep_for(std::chrono::seconds(16));
    }
    catch (const std::exception& e)
    {
        std::cout << "-----------------------------------------" << std::endl;
        std::cout << e.what() << std::endl;
        std::cout << "-----------------------------------------" << std::endl;
    }
    work.reset();
    t.join();
    return 0;
}
