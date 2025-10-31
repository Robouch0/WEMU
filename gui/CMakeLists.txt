cmake_minimum_required(VERSION 3.16)

project(gui VERSION 0.1 LANGUAGES CXX)


set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Multimedia QuickControls2)

qt_standard_project_setup(REQUIRES 6.6.2)
qt_add_resources(RESOURCES resources.qrc)

qt_add_executable(appgui
    src/main.cpp
    src/EmulatorWindow.cpp
    src/EmulatorWindow.hpp
    ../core/src/utils/EmulatorCounter.hpp
    src/input/IInputDevice.hpp
    src/input/KeyboardInputDevice.cpp
    src/input/KeyboardInputDevice.hpp
    src/input/InputManager.hpp
    src/input/InputManager.cpp
    ${RESOURCES}

)
qt_policy(SET QTP0004 NEW)


qt_add_qml_module(appgui
    URI gui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/IntroSequence.qml
        qml/pages/MainMenu.qml
        qml/pages/SettingsPage.qml
        qml/assets/FrenchGuy.mp4
        RESOURCES resources.qrc
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appgui PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appgui
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_include_directories(appgui
        PRIVATE
        ${CMAKE_SOURCE_DIR}/core/src
        ${Qt6Core_INCLUDE_DIRS}
)

target_link_libraries(appgui
    PRIVATE
        Qt6::Core
        Qt6::Quick
        Qt6::Multimedia
        Qt6::QuickControls2
)

include(GNUInstallDirs)
install(TARGETS appgui
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
