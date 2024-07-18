#include <iostream>
#include "../AsioRpc.h"

int main(int argc, char* argv[])
{
    std::shared_ptr<asio::io_context> ioc = std::make_shared<asio::io_context>();
    asio::io_context::work work(*ioc);
    std::thread t([=] { 
        ioc->run(); 
       
    });
    auto rpcServer = std::make_shared<FRpcServer>(ioc);
    rpcServer->Listen();

    t.join();
    return 0;
}