cmake_minimum_required(VERSION 2.6.4)

set(SYS_SRCS
    daemon.c
    file.c
    lock.c
    log.c
    net.c
    sort.c
    string.c
    time.c
    crypt.c
    record.c
    vector.c
    telnet.c
    vbuf.c
    vtkbd.c
    utf8.c
    big5.c
    buffer.c
    thttp.c
    )

file(COPY big5data.tar.bz2 DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(OUTPUT big5.c
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/big5_gen.py > ${CMAKE_CURRENT_BINARY_DIR}/big5.c
    )

add_library(cmsys STATIC ${SYS_SRCS})
