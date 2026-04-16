file(GLOB_RECURSE TEST_SOURCES "*.cpp")

add_executable(wemu_tests ${TEST_SOURCES}
        logical/test_extsb.cpp
        logical/test_extsh.cpp)

target_include_directories(wemu_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_options(wemu_tests PRIVATE -Wno-overflow)

target_link_libraries(wemu_tests
        PRIVATE
        core
        gtest
        gtest_main
)

include(GoogleTest)
add_test(NAME wemu_tests COMMAND wemu_tests)

if(ENABLE_COVERAGE)
    target_compile_options(wemu_tests PRIVATE --coverage -O0 -g)
    target_link_options(wemu_tests PRIVATE --coverage)
endif()
