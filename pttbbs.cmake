##########
# BBSHOME
##########

set(BBSHOME "${HOME}")
if("${BBSHOME}" STREQUAL "")
    set(BBSHOME /home/bbs)
endif()

##########
# Command build flags
##########

set(PTT_WARN "-W -Wall -Wunused -Wno-missing-field-initializers")
set(PTT_CFLAGS "${PTT_WARN} -pipe -DBBSHOME='\"${BBSHOME}\"' -I${CMAKE_SOURCE_DIR}/include")
set(PTT_CXXFLAGS "${PTT_WARN} -pipe -DBBSHOME='\"${BBSHOME}\"' -I${CMAKE_SOURCE_DIR}/include")
set(PTT_LDFLAGS "-Wl,--as-needed")

if(${CMAKE_C_COMPILER_ID} MATCHES "Clang")
    message(STATUS "using clang")
    set(PTT_CFLAGS "${PTT_CFLAGS} -Qunused-arguments -Wno-parentheses-equality -fcolor-diagnostics -Wno-invalid-source-encoding")
endif()

##########
# Platform specific build flags
##########

if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    message(STATUS "on MacOSX")
    find_package(curses REQUIRED)

    set(PTT_LDFLAGS "-Wl")

    set(PTT_CFLAGS "${PTT_CFLAGS} -I/opt/local/include -I/usr/local/opt/ncurses/include -DNEED_SETPROCTITLE")
    set(PTT_CXXFLAGS "${PTT_CXXFLAGS} -I/opt/local/include -I/usr/local/opt/ncurses/include")
    set(PTT_LDFLAGS "${PTT_LDFLAGS} -L/usr/local/opt/ncurses/lib")
    set(PTT_LDLIBS iconv ${CURSES_LIBRARIES})
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    message(STATUS "on FreeBSD")
    set(PTT_CFLAGS "${PTT_CFLAGS} -I/usr/local/include")
    set(PTT_CXXFLAGS "${PTT_CXXFLAGS} -I/usr/local/include")
    set(PTT_LDFLAGS "${PTT_LDFLAGS} -L/usr/local/lib")
    set(PTT_LDLIBS kvm iconv)
endif()

##########
# Profiling
##########

if(${PROFILING})
    set(PTT_CFLAGS "${PTT_CFLAGS} -pg")
    set(PTT_CXXFLAGS "${PTT_CXXFLAGS} -pg")
    set(PTT_LDFLAGS "${PTT_LDFLAGS} -pg")
    set(NO_OMITFP yes)
    set(NO_FORK yes)
endif()

##########
# Debug
##########

if(${DEBUG})
    set(GDB 1)
    set(PTT_CFLAGS "${PTT_CFLAGS} -DDEBUG")
    set(PTT_CXXFLAGS "${PTT_CXXFLAGS} -DDEBUG")
endif()

##########
# flags
##########

if(${GDB})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 ${PTT_CFLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 ${PTT_CXXFLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 ${PTT_LDFLAGS}")
    # set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} -O0 ${PTT_LDFLAGS}")
    SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)
    set(LDLIBS ${PTT_LDLIBS})
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Os ${PTT_CFLAGS} ${EXT_CFLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Os ${PTT_CXXFLAGS} ${EXT_CXXFLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Os ${PTT_LDFLAGS}")
    # set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} -O0 ${PTT_LDFLAGS}")
    SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)
    set(LDLIBS ${PTT_LDLIBS})

    if(${OMITFP})
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fomit-frame-pointer")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fomit-frame-pointer")
    endif()
endif()

##########
# No-fork
##########

if(${NO_FORK})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNO_FORK")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNO_FORK")
endif()

##########
# libevent
##########

execute_process(COMMAND pkg-config --cflags libevent
    OUTPUT_VARIABLE LIBEVENT_CFLAGS
    )
execute_process(COMMAND pkg-config --libs-only-L libevent
    OUTPUT_VARIABLE LIBEVENT_LIBS_L
    )
execute_process(COMMAND pkg-config --libs-only-l libevent
    OUTPUT_VARIABLE LIBEVENT_LIBS_l
    )

##########
# LDLIBS
##########

set(LDLIBS ${LDLIBS} cmbbs cmsys osdep)

##########
# var.h
##########

add_custom_command(OUTPUT ${PROJECT_SOURCE_DIR}/include/var.h
    COMMAND perl ${PROJECT_SOURCE_DIR}/util/parsevar.pl < ${CMAKE_SOURCE_DIR}/mbbsd/var.c > ${CMAKE_SOURCE_DIR}/include/var.h
    DEPENDS ${PROJECT_SOURCE_DIR}/mbbsd/var.c
    )

add_custom_target(VAR_H ALL
    DEPENDS ${PROJECT_SOURCE_DIR}/include/var.h)
