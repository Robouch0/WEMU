cmake_minimum_required(VERSION 3.18)
project(WemuEmulator VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(glfw3 CONFIG REQUIRED)
#find_package(glslc CONFIG REQUIRED)
#find_package(glm-devel CONFIG REQUIRED)



add_subdirectory(libs/cpu_IR)
add_subdirectory(libs/gfx_IR)
add_subdirectory(core)
add_subdirectory(gui)
add_subdirectory(vulkan)
