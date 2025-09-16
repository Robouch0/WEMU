add_executable(wemu_core src/coreDummy.cpp)

target_link_libraries(wemu_core PRIVATE gfx_frontend cpu_frontend)

target_include_directories(wemu_core PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/cpu_IR/include
        ${CMAKE_SOURCE_DIR}/libs/gfx_IR/include
)

target_compile_features(wemu_core PUBLIC cxx_std_20)
