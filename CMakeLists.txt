cmake_minimum_required(VERSION 3.22)

set(VCPKG_TARGET_TRIPLET "x64-windows")
set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

project(AsioRpc)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "ON" FORCE)

add_executable(Example src/Main.cpp 
src/RpcClient.h
src/RpcServer.h
src/TcpClient.h
src/Connection.h
src/TcpContext.h
src/TcpServer.h
AsioRpc.h)

find_package(boost_describe CONFIG REQUIRED)
find_package(boost_asio CONFIG REQUIRED)
find_package(boost_dll CONFIG REQUIRED)
find_package(boost_interprocess CONFIG REQUIRED)
find_package(boost_json CONFIG REQUIRED)
target_link_libraries(Example PRIVATE Boost::asio Boost::describe Boost::dll Boost::interprocess Boost::json)
