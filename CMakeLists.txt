set(VCPKG_TARGET_TRIPLET "x64-windows")
set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

cmake_minimum_required(VERSION 3.22)

project(Skynet VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "ON" FORCE)

add_executable(BxZnComputerClient src/Main.cpp)

