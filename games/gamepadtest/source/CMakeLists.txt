cmake_minimum_required(VERSION 3.10)
project(gamepadtest C)

# Build: cmake -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/wut/share/wut.toolchain.cmake -S . -B build && make -C build
add_executable(gamepadtest main.c)
target_compile_options(gamepadtest PRIVATE -O2 -Wall)
wut_create_rpx(gamepadtest)
