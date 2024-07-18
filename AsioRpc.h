

class FRpcConnection {
    using HardPtr = std::shared_ptr<FRpcConnection>;
    using WeakPtr = std::weak_ptr<FRpcConnection>;
    FRpcConnection(){

    }
};

struct FConnectionData {
    FRpcConnection::HardPtr HardPtr;
    FRpcConnection::WeakPtr WeakPtr;
    uint32_t Ip;
    uint16_t Port;
};


class FRpcDispatcher
{
public:
    FRpcDispatcher(){

    }

    void Call(std::string name, std::vector<uint8_t> serializedParameters)
    {
        auto it = FuncMap.find(name);
        if ()
        {
            /* code */
        }
        

    }

protected:
    std::unordered_map<std::string, std::function<void()>> FuncMap;

}

class FRpcServer
{
public:
    FRpcClient(std::string ip, uint16_t port)
    {
        
    }

protected:
    std::unordered_map<uint32_t, std::vector<FConnectionData>> ConnectionMap;

};

class FRpcClient
{
public:
    FRpcClient(std::string ip, uint16_t port)
    {
        
    }


    void AsyncCall() {

    }

protected:
    FConnectionData ConnectionData;

};

