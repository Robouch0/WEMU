add_library(cpu_frontend STATIC
        src/translatorCPU.cpp
)

target_include_directories(cpu_frontend
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(cpu_frontend PUBLIC cxx_std_20)
target_compile_options(cpu_frontend PRIVATE -Wall -Wextra -Wpedantic)
