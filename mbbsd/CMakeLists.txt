cmake_minimum_required(VERSION 2.6.4)

set(CORESRCS
    bbs.c
    announce.c
    read.c
    board.c
    brc.c
    mail.c
    record.c
    fav.c
    )

set(ABUSESRCS
    captcha.c
    )

set(ACCSRCS
    user.c
    acl.c
    register.c
    passwd.c
    emaildb.c
    )

set(NETSRCS
    mbbsd.c
    io.c
    term.c
    telnet.c
    nios.c
    )

set(TALKSRCS
    friend.c
    talk.c
    ccw.c
    )

set(UTILSRCS
    stuff.c
    kaede.c
    convert.c
    name.c
    syspost.c
    cache.c
    cal.c
    )

set(UISRCS
    menu.c
    vtuikit.c
    psb.c
    )

set(PAGERSRCS
    more.c
    pmore.c
    )

set(PLUGSRCS
    ordersong.c
    angel.c
    timecap.c
    )

set(CHESSSRCS
    chess.c
    chc.c
    chc_tab.c
    ch_go.c
    ch_gomo.c
    ch_dark.c
    ch_reversi.c
    ch_conn6.c
    )

set(GAMESRCS
    chicken.c
    gamble.c
    )

set(OTHERSRCS
    admin.c
    assess.c
    edit.c
    xyz.c
    var.c
    vote.c
    voteboard.c
    comments.c
    )

set(SRCS
    ${OTHERSRCS}
    ${CORESRCS}
    ${ABUSESRCS}
    ${ACCSRCS}
    ${NETSRCS}
    ${TALKSRCS}
    ${UTILSRCS}
    ${UISRCS}
    ${PAGERSRCS}
    ${PLUGSRCS}
    ${CHESSSRCS}
    ${GAMESRCS})

set(TESTSZSRCS
    testsz.c
    )

if(${DIET})
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${PROJECT_SOURCE_DIR}/common/diet")
    set(LDLIBS ${LDLIBS} "diet")
    set(DIETCC "diet -Os")
endif()

# reduce .bss align overhead
if(NOT (${DEBUG}) AND NOT (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin"))
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--sort-common")
endif()

# bbslua
if(${USE_BBSLUA})
    if(${CMAKE_SYSTEM_NAME} == "FreeBSD")
        set(LUA_PKG_NAME "lua-5.1")
        set(LDLIBS "${LDLIBS} -Wl,--no-as-needed")
    else()
        set(LUA_PKG_NAME "lua5.1")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -E pkg-config --cflags "${LUA_PKG_NAME}" OUTPUT_VARIABLE LUA_CFLAGS)
    execute_process(COMMAND ${CMAKE_COMMAND} -E pkg-config --libs "${LUA_PKG_NAME}" OUTPUT_VARIABLE LUA_LIBS)

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${LUA_CFLAGS}")
    set(LDLIBS "${LDLIBS} ${LUA_LIBS}")
    set(SRCS ${SRCS} bbslua.c bbsluaext.c)
endif()

# pfterm
if(${USE_PFTERM})
    set(SRCS ${SRCS} pfterm.c)
else()
    set(SRCS ${SRCS} screen.c)
endif()

add_executable(testsz 
    ${TESTSZSRCS}
    )

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vers.c
    COMMAND sh ${PROJECT_SOURCE_DIR}/util/newvers.sh
    DEPENDS ${PROJECT_SOURCE_DIR}/util/newvers.sh
    )
add_executable(mbbsd vers.c ${SRCS})
target_link_libraries(mbbsd ${LDLIBS})
