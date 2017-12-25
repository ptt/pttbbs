cmake_minimum_required(VERSION 2.6.4)

project(pttbbs)

message(STATUS "CMAKE_SYSTEM: ${CMAKE_SYSTEM}")
message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
message(STATUS "CMAKE_SYSTEM_VERSION: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_C_COMPILER_ID: ${CMAKE_C_COMPILER_ID}")

##########
# pttbbs.cmake
##########

include("pttbbs.cmake")

##########
# for pttbbs.conf
##########

if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
    message(STATUS "CMAKE SYSTEM PROCESSOR is x86_64")
    set(SHMALIGNEDSIZE 1)
    set(SHM_MB_SIZE 4)

    set(TIMET64 1)
elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "ia64")
    message("CMAKE SYSTEM PROCESSOR is ia64")
    set(SHMALIGNEDSIZE 1)
    set(SHM_MB_SIZE 256)

    set(TIMET64 1)
endif()

message(STATUS "SHM_MB_SIZE: ${SHM_MB_SIZE}")

configure_file(pttbbs.cmake.conf ${CMAKE_SOURCE_DIR}/pttbbs.conf @ONLY)

##########
# sub-dirs
##########

include_directories(${PROJECT_SOURCE_DIR}/include)
add_subdirectory(common)
add_subdirectory(mbbsd)
add_subdirectory(util)
add_subdirectory(tests)
