##
## EPITECH PROJECT, 2025
## WemuEmulator
## File description:
## CMakeLists
##

cmake_minimum_required(VERSION 3.28.1)

set(CMAKE_CXX_STANDARD 23)

add_executable(core
        src/main.cpp
        src/binary/Binary.hpp
        src/utils/BeDecoder.hpp
        src/exception/Exception.hpp
        src/exception/Exception.cpp
        src/binary/Loader.hpp
        src/binary/Loader.cpp
        src/utils/BeDecoder.cpp
)

target_include_directories(core PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
