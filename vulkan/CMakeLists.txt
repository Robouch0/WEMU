cmake_minimum_required(VERSION 3.28.0)
project(WEMU)

find_package(Vulkan REQUIRED)

if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan not found. Install with: sudo apt install libvulkan-dev")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW3 REQUIRED glfw3)

set(CMAKE_EXPORT_COMPILE_COMMANDS)
set(SHADER_STAMP ${CMAKE_CURRENT_BINARY_DIR}/shaders_compiled.stamp)

add_custom_command(
        OUTPUT ${SHADER_STAMP}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/compile.sh
        COMMAND ${CMAKE_COMMAND} -E touch ${SHADER_STAMP}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/compile.sh
        COMMENT "Shader compilation"
        VERBATIM
        USES_TERMINAL
)

add_custom_target(compile_shaders ALL
        DEPENDS ${SHADER_STAMP}
)

add_executable(WEMU
        src/buffer.cpp
        src/cleanup.cpp
        src/commandPool.cpp
        src/descriptor.cpp
        src/framebuffers.cpp
        src/graphicsPipelineInit.cpp
        src/image.cpp
        src/instance.cpp
        src/logicalDevice.cpp
        src/main.cpp
        src/pick.cpp
        src/renderPass.cpp
        src/semaphoreAndFence.cpp
        src/shader.cpp
        src/surface.cpp
        src/swapChain.cpp
        src/update.cpp
        src/window.cpp
)

target_include_directories(WEMU PRIVATE
        ${Vulkan_INCLUDE_DIRS}
        ${GLFW3_INCLUDE_DIRS}
        .
)

target_link_libraries(WEMU PRIVATE
        Vulkan::Vulkan
        ${GLFW3_STATIC_LIBRARIES}
)

target_compile_definitions(WEMU PRIVATE
        PROJECT_RELATIVE_PATH="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_dependencies(WEMU compile_shaders)