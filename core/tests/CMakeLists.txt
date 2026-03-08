file(GLOB TEST_SOURCES "*.cpp")

add_executable(wemu_tests ${TEST_SOURCES})

target_compile_options(wemu_tests PRIVATE -Wno-overflow)

target_link_libraries(wemu_tests
        PRIVATE
        core
        gtest
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(wemu_tests)
