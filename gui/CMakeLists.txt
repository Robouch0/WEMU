cmake_minimum_required(VERSION 3.16)

project(wemu_gui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(wemu_gui
        src/main.cpp
)

target_link_libraries(wemu_gui
        PRIVATE
        cpu_frontend
        gfx_frontend
        Qt6::Core
        Qt6::Widgets
)
