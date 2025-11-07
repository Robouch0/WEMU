cmake_minimum_required(VERSION 3.28.0)
project(WEMU)
set(CMAKE_EXPORT_COMPILE_COMMANDS)
add_executable(WEMU vulkan/src/main.cpp)