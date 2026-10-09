cmake_minimum_required(VERSION 3.16)

project(gui VERSION 0.1 LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.6.2)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)

qt_add_executable(appgui
    src/main.cpp
    src/input/KeyboardInput.cpp
    src/input/SDLGamepadInput.cpp
    src/input/InputManager.cpp
    src/input/InputProfileManager.cpp
    src/library/TitleScanner.cpp
    src/emulator/EmulatorLauncher.cpp
)


target_sources(appgui PRIVATE
        src/input/IInputDevice.hpp
        src/input/KeyboardInput.hpp
        src/input/InputManager.hpp
        src/input/SDLGamepadInput.hpp
        src/input/InputProfile.hpp
        src/input/InputProfileManager.hpp
        src/library/TitleScanner.hpp
)

qt_add_qml_module(appgui
    URI gui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/IntroSequence.qml
        qml/pages/MainMenu.qml
        qml/pages/SettingsPage.qml
        qml/components/ControllerPreview.qml
        qml/components/ControllerButton.qml
        qml/components/DpadButton.qml
        qml/components/ControllerStick.qml
        qml/components/GamepadPreview.qml
        qml/components/BindingRow.qml
)

qt_add_resources(APPGUI_QRC resources.qrc)
target_sources(appgui PRIVATE ${APPGUI_QRC})

set_target_properties(appgui PROPERTIES
    WIN32_EXECUTABLE TRUE
)

target_include_directories(appgui
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../core/src/
        ${CMAKE_CURRENT_SOURCE_DIR}/src/input
        ${CMAKE_CURRENT_SOURCE_DIR}/src/library
        ${CMAKE_CURRENT_SOURCE_DIR}/src/emulator
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx
        ${Qt6Core_INCLUDE_DIRS}
)

target_link_libraries(appgui
    PRIVATE
        Qt6::Core
        Qt6::Quick
        Qt6::Gui
        Qt6::QuickControls2
        SDL2::SDL2
        SDL2_image::SDL2_image
)
target_compile_definitions(appgui PRIVATE WEMU_CORE_BINARY="$<TARGET_FILE:wemu>")
add_dependencies(appgui wemu)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/WEMU.desktop" CONTENT
"[Desktop Entry]\nType=Application\nName=WEMU\nComment=Game library and emulator\nExec=\"$<TARGET_FILE:appgui>\"\nIcon=applications-games\nTerminal=false\nCategories=Game;Emulator;\n")

add_executable(wemu_library_tests tests/TitleScannerTest.cpp src/library/TitleScanner.cpp src/library/TitleScanner.hpp)
target_include_directories(wemu_library_tests PRIVATE src/library)
target_link_libraries(wemu_library_tests PRIVATE Qt6::Core Qt6::Gui SDL2::SDL2 SDL2_image::SDL2_image)
enable_testing()
add_test(NAME wemu_library_tests COMMAND wemu_library_tests)
add_executable(wemu_launch_environment_tests tests/LaunchEnvironmentTest.cpp)
target_include_directories(wemu_launch_environment_tests PRIVATE src/emulator)
target_link_libraries(wemu_launch_environment_tests PRIVATE Qt6::Core)
add_test(NAME wemu_launch_environment_tests COMMAND wemu_launch_environment_tests)

include(GNUInstallDirs)
install(TARGETS appgui
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
