add_library(gfx_frontend STATIC
        src/translatorGFX.cpp
)

target_include_directories(gfx_frontend
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gfx_frontend PUBLIC cxx_std_20)
target_compile_options(gfx_frontend PRIVATE -Wall -Wextra -Wpedantic)
