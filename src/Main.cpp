#pragma once
#include "TcpClient.h"
#include "TcpServer.h"
#include "RpcServer.h"
#include "RpcClient.h"
#include "CmdClient.h"

using namespace Cpp;

std::shared_ptr<FCmdClient> testCmdClient(asio::io_context& ioc) {

    std::shared_ptr<FCmdClient> cmdClient = std::make_shared<FCmdClient>(ioc);
    //cmdClient->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
    cmdClient->GetTcpContext()->ConnectedFunc = [](FTcpConnection* connection) {
        std::cout << "ConnectedFunc" << std::endl;
        connection->SetHeartbeatData({ 'c', 'm', 'd' });
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
    std::vector<std::thread> threads;
    for (size_t i = 0; i < 6; i++)
    {
        threads.push_back(std::thread([&] { ioc.run(); }));
    }
    try
    {
        std::atomic<int64_t> sayCounter = 0;
        std::atomic<int64_t> respCounter = 0;

        std::chrono::time_point t1 = std::chrono::steady_clock::now();
        std::shared_ptr<FRpcServer> rpcServer = std::make_shared<FRpcServer>(ioc);
        rpcServer->GetTcpContext()->LogFunc = [](const char* msg) { };
        rpcServer->RefRpcDispatcher()->AddFunc("echo", [&, rpcDispatcher = rpcServer->RefRpcDispatcher()](std::string cmd) -> void {
            std::cout << "server echo > " << cmd << std::endl;
            rpcDispatcher->Call("127.0.0.1", "echo", [] {
                std::cout << "client echo < " << std::endl;
            }, cmd);
        });
        rpcServer->RefRpcDispatcher()->AddFunc("say", [&, rpcDispatcher = rpcServer->RefRpcDispatcher()](std::string cmd) -> asio::awaitable<int64_t> {
            //std::cout << "server say > " << cmd << std::endl;
            co_return sayCounter++;
        });
        rpcServer->Start();
        std::vector<std::shared_ptr<FRpcClient>> rpcClients;
        for (size_t i = 0; i < 256; i++)
        {
            std::shared_ptr<FRpcClient> rpcClient = std::make_shared<FRpcClient>(ioc);
            rpcClient->GetTcpContext()->LogFunc = [](const char* msg) { };
            rpcClient->RefRpcDispatcher()->AddFunc("echo", [rpcServer = rpcServer.get()](std::string cmd) -> asio::awaitable<void> {
                std::cout << "client echo > " << cmd << std::endl;
                co_return;
            });
            rpcClient->RefRpcDispatcher()->SetAttachedFunc([=, &respCounter, rpcDispatcher = rpcClient->RefRpcDispatcher()](FTcpConnection* connection) {
                for (size_t j = 0; j < 256; j++)
                {
                    rpcDispatcher->Call(std::shared_ptr<FTcpConnection>(), "say", [=, &respCounter]() -> asio::awaitable<void> {
                        respCounter++;
                        co_return;
                    }, "client" + std::to_string(i));
                }
            });
            rpcClient->Start();
            rpcClients.push_back(rpcClient);
        }

        std::shared_ptr<FTcpServer> tcpServer = std::make_shared<FTcpServer>(ioc);
        //tcpServer->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
        tcpServer->GetTcpContext()->ConnectedFunc = [](FTcpConnection* connection) { connection->SetHeartbeatData({ 't', 'c', 'p', });  };
        tcpServer->GetTcpContext()->DisconnectedFunc = [](FTcpConnection* connection) {  };
        tcpServer->GetTcpContext()->RecvDataFunc = [](FTcpConnection* connection, std::vector<uint8_t> buffer) {
            printf("client: ");
            for (size_t i = 0; i < buffer.size(); i++)
            {
                printf("%c", buffer[i]);
            }
            printf("\n");
        };
        tcpServer->Start(asio::ip::address_v4::any(), 7777);
        while (respCounter != 256 * 256)
        {
            std::this_thread::yield();
        }

        while (respCounter != sayCounter) {
            std::this_thread::yield();
        }
        auto val = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        std::cout << "rpc : "<< respCounter << ", cost : " << val << std::endl;


        //std::shared_ptr<FTcpClient> tcpClient = std::make_shared<FTcpClient>(ioc);
        ////tcpClient->GetTcpContext()->LogFunc = [](const char* msg) { std::cout << msg << std::endl; };
        //tcpClient->GetTcpContext()->ConnectedFunc = [](FTcpConnection* connection) { 
        //    std::cout << "ConnectedFunc" << std::endl;
        //    connection->SetHeartbeatData({ 't', 'c', 'p', });
        //};
        //tcpClient->GetTcpContext()->DisconnectedFunc = [](FTcpConnection* connection) { std::cout << "DisconnectedFunc" << std::endl; };
        //tcpClient->GetTcpContext()->RecvDataFunc = [](FTcpConnection* connection, std::vector<uint8_t> buffer) {
        //    printf("server: ");
        //    for (size_t i = 0; i < buffer.size(); i++)
        //    {
        //        printf("%c", buffer[i]);
        //    }
        //    printf("\n");
        //};
        //tcpClient->Start(asio::ip::address_v4::loopback(), 7777);

        //auto cmdClient = testCmdClient(ioc);
    }
    catch (const std::exception& e)
    {
        std::cout << "-----------------------------------------" << std::endl;
        std::cout << e.what() << std::endl;
        std::cout << "-----------------------------------------" << std::endl;
    }
    work.reset();
    for (auto& thread : threads)
    {
        thread.join();
    }
    return 0;
}
