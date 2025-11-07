add_library(core STATIC
        src/coreDummy.cpp
)

target_link_libraries(core
        PUBLIC cpu_frontend gfx_frontend
)

target_include_directories(core
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(core PUBLIC cxx_std_20)
