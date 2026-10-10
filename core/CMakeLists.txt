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
find_package(Threads REQUIRED)
find_program(WEMU_RUNTIME_GLSLANG glslangValidator)
find_program(WEMU_RUNTIME_SPIRV_VAL spirv-val)
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found. Install it or set VULKAN_SDK.")
endif()
find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec>=60 libavutil libswscale)

# ------------------------------------------------------------
# 1. OPTIONS
# ------------------------------------------------------------
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
option(WEMU_BUILD_GUI "Build the desktop game library" OFF)
option(WEMU_ENABLE_LLVM "Build experimental PPC native block compiler (LLVM 19)" OFF)
if(WEMU_BUILD_GUI AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(../gui gui)
endif()

# ------------------------------------------------------------
# 2. CORE LIBRARY
# ------------------------------------------------------------
add_library(core STATIC
        src/binary/Binary.hpp
        src/utils/BeDecoder.hpp
        src/utils/Logger.hpp
        src/exception/Exception.hpp
        src/exception/Exception.cpp
        src/binary/Loader.hpp
        src/binary/Loader.cpp
        src/utils/BeDecoder.cpp
        src/utils/Diagnostics.hpp
        src/utils/Diagnostics.cpp
        src/hle/MemHeap.hpp
        src/hle/MemHeap.cpp
        src/hle/CoreinitExtra.hpp
        src/hle/CoreinitExtra.cpp
        src/hle/H264.cpp
        src/video/H264Decoder.cpp
        src/hle/Ax.hpp
        src/hle/Ax.cpp
        src/hle/AsyncCallbacks.hpp
        src/hle/AsyncCallbacks.cpp
        src/hle/NnOnline.hpp
        src/hle/NnOnline.cpp
        src/cpu/interpreter/Interpreter.hpp
        src/cpu/interpreter/Interpreter.cpp
        src/cpu/recompiler/Block.cpp
        src/cpu/instructions/arithmetic/Add.cpp
        src/cpu/instructions/Sc.cpp
        src/cpu/types/Field.hpp
        src/cpu/types/Instruction.hpp
        src/cpu/types/EncodedInstruction.hpp
        src/cpu/interpreter/SyscallHandler.hpp
        src/lib/coreinit/Coreinint.hpp
        src/cpu/memory/Memory.hpp
        src/cpu/instructions/Branch.cpp
        src/cpu/instructions/logical/Or.cpp
        src/cpu/instructions/logical/And.cpp
        src/cpu/instructions/arithmetic/Mul.cpp
        src/cpu/instructions/arithmetic/Div.cpp
        src/cpu/instructions/arithmetic/Sub.cpp
        src/cpu/instructions/logical/Xor.cpp
        src/cpu/instructions/arithmetic/Neg.cpp
        src/cpu/instructions/load/Load.cpp
        src/cpu/instructions/load/LoadExtra.cpp
        src/cpu/instructions/store/Store.cpp
        src/cpu/instructions/store/StoreExtra.cpp
        src/cpu/instructions/CrLogical.cpp
        src/cpu/instructions/Trap.cpp
        src/cpu/instructions/spr/Spr.cpp
        src/cpu/instructions/spr/Cache.cpp
        src/cpu/instructions/shift/Shift.cpp
        src/cpu/instructions/shift/Rotate.cpp
        src/cpu/instructions/shift/Extend.cpp
        src/cpu/instructions/float/FloatLoad.cpp
        src/cpu/instructions/float/FloatStore.cpp
        src/cpu/instructions/float/FloatMove.cpp
        src/cpu/instructions/float/FloatConvert.cpp
        src/cpu/instructions/float/FloatCompare.cpp
        src/cpu/instructions/float/FloatArith.cpp
        src/cpu/instructions/float/FloatLoadStoreExtra.cpp
        src/cpu/instructions/float/PairedSingle.cpp
        src/cpu/instructions/BranchExtra.cpp
        src/cpu/instructions/Cmp.cpp
        src/hle/Coreinit.hpp
        src/hle/Coreinit.cpp
        src/hle/Libc.hpp
        src/hle/Libc.cpp
        src/hle/Whb.hpp
        src/hle/Whb.cpp
        src/hle/Gx2.hpp
        src/hle/Gx2.cpp
        src/gfx/Gx2CommandStream.hpp
        src/gfx/Gx2Replayer.hpp
        src/gfx/Gx2Replayer.cpp
        src/gfx/GpuQuadRasterizer.cpp
        src/gfx/GpuRenderGraph.cpp
        src/gfx/LatteVsInterp.cpp
        src/gfx/LatteProgram.cpp
        src/gfx/LatteShaderLowering.cpp
        src/gfx/SpirvCompiler.cpp
        src/gfx/VulkanRasterBackend.cpp
        src/gfx/RasterWorkers.cpp
        src/hle/Fs.hpp
        src/hle/Fs.cpp
        src/hle/StdlibHooks.hpp
        src/hle/StdlibHooks.cpp
        src/hle/Scheduler.hpp
        src/hle/Scheduler.cpp
        src/gfx/Renderer.cpp
        src/ui/PauseMenu.cpp
        src/gfx/init/buffer.cpp
        src/gfx/init/cleanup.cpp
        src/gfx/init/commandPool.cpp
        src/gfx/init/flipTv.cpp
        src/gfx/init/image.cpp
        src/gfx/init/instance.cpp
        src/gfx/init/logicalDevice.cpp
        src/gfx/init/pick.cpp
        src/gfx/init/semaphoreAndFence.cpp
        src/gfx/init/surface.cpp
        src/gfx/init/swapChain.cpp
        src/gfx/init/update.cpp
        src/gfx/init/window.cpp
        src/cpu/instructions/logical/Ext.cpp
        src/cpu/instructions/logical/Cnt.cpp
)

target_include_directories(core PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        ${ZLIB_INCLUDE_DIRS}
        ${Vulkan_INCLUDE_DIRS}
        ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(core PRIVATE
        Threads::Threads
        PkgConfig::FFMPEG
        ${ZLIB_LIBRARIES}
        Vulkan::Vulkan
        ${SDL2_LIBRARIES}
)

target_compile_features(core PUBLIC cxx_std_23)

# Log level: 0=Trace 1=Debug 2=Info 3=Warning 4=Error 5=None (default: 4)
set(WEMU_LOG_LEVEL "4" CACHE STRING "Compile-time log level (0=Trace, 1=Debug, 2=Info, 3=Warning, 4=Error, 5=None)")
target_compile_definitions(core PUBLIC WEMU_LOG_LEVEL=${WEMU_LOG_LEVEL})

if(ENABLE_COVERAGE)
    target_compile_options(core PRIVATE --coverage -O0 -g)
    target_link_libraries(core PUBLIC gcov)
endif()

# ------------------------------------------------------------
# 3. BINAIRE PRINCIPAL
# ------------------------------------------------------------
add_executable(wemu src/main.cpp)
target_link_libraries(wemu PRIVATE core)

# Optional desktop transport; the emulation library and standalone SDL path stay toolkit-independent.
find_package(Qt6 QUIET COMPONENTS Core Network)
if(TARGET Qt6::Core AND TARGET Qt6::Network)
    target_sources(wemu PRIVATE src/frontend/QtHostPresenter.cpp)
    target_link_libraries(wemu PRIVATE Qt6::Core Qt6::Network)
    target_compile_definitions(wemu PRIVATE WEMU_QT_FRONTEND=1)
endif()

# Offline diagnostic lowering; not used to replace unsupported live draws.
add_executable(wemu_shader_lower src/gfx/tools/LowerShader.cpp)
target_link_libraries(wemu_shader_lower PRIVATE core)

if(WEMU_ENABLE_LLVM)
    find_package(LLVM 19.1 REQUIRED CONFIG)
    set(ppc_identity "")
    foreach(ppc_source IN ITEMS src/cpu/recompiler/Block.cpp src/cpu/recompiler/Block.hpp
            src/cpu/recompiler/LlvmBackend.cpp src/cpu/recompiler/LlvmBackend.hpp)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${ppc_source}")
        file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${ppc_source}" ppc_hash)
        string(APPEND ppc_identity "${ppc_hash}")
    endforeach()
    string(SHA256 ppc_fingerprint "${ppc_identity}")
    set_property(SOURCE src/cpu/recompiler/LlvmBackend.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        "WEMU_PPC_COMPILER_FINGERPRINT=\"${ppc_fingerprint}\"")
    target_sources(core PRIVATE src/cpu/recompiler/LlvmBackend.cpp src/cpu/recompiler/Dispatcher.cpp src/cpu/recompiler/ObjectCache.cpp)
    target_include_directories(core SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(core PUBLIC WEMU_HAS_LLVM=1)
    target_link_libraries(core PRIVATE LLVM)
endif()
if(WEMU_RUNTIME_GLSLANG AND WEMU_RUNTIME_SPIRV_VAL)
    target_compile_definitions(core PRIVATE
        WEMU_RUNTIME_GLSLANG="${WEMU_RUNTIME_GLSLANG}"
        WEMU_RUNTIME_SPIRV_VAL="${WEMU_RUNTIME_SPIRV_VAL}")
endif()

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
            --ignore-errors mismatch,unused,inconsistent
            --rc geninfo_unexecuted_blocks=1
            --exclude "*/usr/include/*"
            --output-file ${COVERAGE_RAW}
            COMMAND ${LCOV}
            --remove ${COVERAGE_RAW}
            "${CMAKE_BINARY_DIR}/_deps/*"
            --branch-coverage
            --ignore-errors mismatch,unused,inconsistent
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
