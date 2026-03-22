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
# 1. OPTIONS
# ------------------------------------------------------------
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)

# ------------------------------------------------------------
# 2. CORE LIBRARY
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
        src/cpu/instructions/Or.cpp
)

target_include_directories(core PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(core PRIVATE ${ZLIB_LIBRARIES})

target_compile_features(core PUBLIC cxx_std_20)

if(ENABLE_COVERAGE)
    target_compile_options(core PRIVATE --coverage -O0 -g)
    target_link_libraries(core PUBLIC gcov)
endif()

# ------------------------------------------------------------
# 3. BINAIRE PRINCIPAL
# ------------------------------------------------------------
add_executable(wemu src/main.cpp)
target_link_libraries(wemu PRIVATE core)

if(ENABLE_COVERAGE)
    target_compile_options(wemu PRIVATE --coverage -O0 -g)
    target_link_options(wemu PRIVATE --coverage)
endif()

# ------------------------------------------------------------
# 4. GOOGLE TEST
# ------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ------------------------------------------------------------
# 5. TESTS UNITAIRES
# ------------------------------------------------------------
add_subdirectory(tests)

# ------------------------------------------------------------
# 6. TARGET coverage
# ------------------------------------------------------------
if(ENABLE_COVERAGE)
    find_program(LCOV lcov REQUIRED)
    find_program(GENHTML genhtml)

    set(COVERAGE_RAW    ${CMAKE_BINARY_DIR}/coverage_raw.info)
    set(COVERAGE_INFO   ${CMAKE_BINARY_DIR}/coverage.info)
    set(COVERAGE_REPORT ${CMAKE_BINARY_DIR}/coverage_report)

    add_custom_target(coverage
            COMMENT "Generating coverage report..."
            COMMAND ${LCOV} --zerocounters --directory ${CMAKE_BINARY_DIR}
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LCOV}
            --capture
            --directory ${CMAKE_BINARY_DIR}
            --branch-coverage
            --ignore-errors mismatch,unused
            --rc geninfo_unexecuted_blocks=1
            --exclude "*/usr/include/*"
            --output-file ${COVERAGE_RAW}
            COMMAND ${LCOV}
            --remove ${COVERAGE_RAW}
            "${CMAKE_BINARY_DIR}/_deps/*"
            --branch-coverage
            --ignore-errors mismatch,unused
            --output-file ${COVERAGE_INFO}
            COMMAND ${LCOV} --list ${COVERAGE_INFO}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    if(GENHTML)
        add_custom_target(coverage-html
                COMMENT "Generating HTML coverage report..."
                COMMAND ${GENHTML}
                ${COVERAGE_INFO}
                --branch-coverage
                --output-directory ${COVERAGE_REPORT}
                COMMAND ${CMAKE_COMMAND} -E echo
                "Report available at: ${COVERAGE_REPORT}/index.html"
                DEPENDS coverage
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    endif()
endif()
