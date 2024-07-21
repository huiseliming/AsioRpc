#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include "TcpContext.h"

namespace Cpp{

    class FRpcDispatcher {
    public:
        FRpcDispatcher() {

        }

        template<typename Func>
        bool AddFunc(std::string name, Func&& func) {
            using FuncArgs = boost::callable_traits::args_t<Func>;
            auto f = [func = std::forward<Func>(func)](json::value& args) {
                std::apply(func, json::value_to<FuncArgs>(args));
                };
            return FuncMap.insert(std::make_pair(name, f)).second;
        }

        std::unordered_map<std::string, std::function<void(json::value&)>> FuncMap;
    };
}
