cmake_minimum_required(VERSION 3.18)
project(WemuGUI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Qml)

qt_standard_project_setup()

qt_add_executable(wemu_gui
        src/main.cpp
        ../core/src/utils/EmulatorState.hpp
        resources.qrc
)

target_link_libraries(wemu_gui
        PRIVATE
        core
        cpu_frontend
        gfx_frontend
        Qt6::Core
        Qt6::Quick
        Qt6::Qml
)
