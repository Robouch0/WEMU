##
## EPITECH PROJECT, 2025
## WemuEmulator
## File description:
## CMakeLists
##

cmake_minimum_required(VERSION 3.28.1)
project(WemuEmulator)

set(CMAKE_CXX_STANDARD 23)

find_package(ZLIB REQUIRED)

# ------------------------------------------------------------
# 1. CORE LIBRARY
# ------------------------------------------------------------
add_library(core STATIC
        src/binary/Binary.hpp
        src/utils/BeDecoder.hpp
        src/exception/Exception.hpp
        src/exception/Exception.cpp
        src/binary/Loader.hpp
        src/binary/Loader.cpp
        src/utils/BeDecoder.cpp
        src/cpu/interpreter/Interpreter.hpp
        src/cpu/interpreter/Interpreter.cpp
        src/cpu/instructions/Add.cpp
        src/cpu/instructions/Sc.cpp
        src/cpu/types/Field.hpp
        src/cpu/types/Instruction.hpp
        src/cpu/types/EncodedInstruction.hpp
        src/cpu/interpreter/SyscallHandler.hpp
        src/lib/coreinit/Coreinint.hpp
        src/cpu/memory/Memory.hpp
        src/cpu/instructions/Branch.cpp
)

target_include_directories(core PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(core PRIVATE ${ZLIB_LIBRARIES})

# ------------------------------------------------------------
# 2. BINAIRE PRINCIPAL
# ------------------------------------------------------------
add_executable(wemu src/main.cpp)
target_link_libraries(wemu PRIVATE core)

# ------------------------------------------------------------
# 3. GOOGLE TEST
# ------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ------------------------------------------------------------
# 4. TESTS UNITAIRES
# ------------------------------------------------------------
add_subdirectory(tests)

target_compile_features(core PUBLIC cxx_std_20)
