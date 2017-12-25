cmake_minimum_required(VERSION 2.6.4)

set(OSDEP_SRCS
    cpuload.c
    proctitle.c
    strlcat.c
    strlcpy.c
    memusage.c
    )

add_library(osdep STATIC ${OSDEP_SRCS})
link_directories(${CMAKE_CURRENT_BINARY_DIR})