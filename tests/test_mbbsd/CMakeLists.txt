cmake_minimum_required(VERSION 2.6.4)

include_directories(${PROJECT_SOURCE_DIR}/include)

link_directories(${PROJECT_SOURCE_DIR}/common/bbs)
link_directories(${PROJECT_SOURCE_DIR}/common/sys)
link_directories(${PROJECT_SOURCE_DIR}/common/osdep)

add_executable(test_dummy
    test_dummy.cc
    )

target_link_libraries(test_dummy gtest gtest_main cmbbs cmsys osdep)
