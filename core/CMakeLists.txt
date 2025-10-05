add_library(core STATIC
        src/coreDummy.cpp
        src/utils/Loader.cpp
)

target_link_libraries(core
        PUBLIC cpu_frontend gfx_frontend
)

target_include_directories(core
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/utils/
)
add_executable(testLoader
        src/coreDummy.cpp
)

target_link_libraries(testLoader
    PUBLIC core cpu_frontend gfx_frontend
)
target_compile_features(core PUBLIC cxx_std_20)
