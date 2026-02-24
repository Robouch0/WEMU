cmake_minimum_required(VERSION 3.16)

project(gui VERSION 0.1 LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.6.2)
qt_add_resources(RESOURCES resources.qrc)
find_package(SDL2 REQUIRED)

qt_add_executable(appgui
    src/main.cpp
    src/input/KeyboardInput.cpp
    src/input/SDLGamepadInput.cpp
    src/input/InputManager.cpp
    ${RESOURCES}

)

target_sources(appgui PRIVATE
        src/input/IInputDevice.hpp
        src/input/KeyboardInput.hpp
        src/input/InputManager.hpp
        src/input/SDLGamepadInput.hpp
)

#qt_policy(SET QTP0004 NEW)


qt_add_qml_module(appgui
    URI gui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/IntroSequence.qml
        qml/pages/MainMenu.qml
        qml/pages/SettingsPage.qml
        qml/ControllerPreview.qml
        qml/GamepadPreview.qml
#        qml/assets/FrenchGuy.mp4
        RESOURCES resources.qrc
)

set_target_properties(appgui PROPERTIES
    WIN32_EXECUTABLE TRUE
)

target_include_directories(appgui
        PRIVATE
        ${CMAKE_SOURCE_DIR}/core/src/
        ${CMAKE_SOURCE_DIR}/gui/src/input
        ${Qt6Core_INCLUDE_DIRS}
)

target_link_libraries(appgui
    PRIVATE
        Qt6::Core
        Qt6::Quick
        Qt6::Gui
#        Qt6::Multimedia
        Qt6::QuickControls2
        SDL2::SDL2
#        Qt6::Widgets
)

include(GNUInstallDirs)
install(TARGETS appgui
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
