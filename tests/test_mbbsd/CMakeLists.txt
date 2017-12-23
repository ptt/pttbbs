cmake_minimum_required(VERSION 2.6.4)

add_executable(test_dummy
    test_dummy.cc
    )

target_link_libraries(test_dummy gtest gtest_main ${LDLIBS})
