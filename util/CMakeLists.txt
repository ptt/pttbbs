cmake_minimum_required(VERSION 2.6.4)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPTTBBS_UTIL")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(BBSBASE "${PROJECT_SOURCE_DIR}/include/var.h")
set(UTILSRCS
    util_var.c
    )

set(CPROG_WITH_UTIL
    boardlist
    post
    poststat
    account
    deluserfile
    expire
    mandex
    broadcast
    openticket
    topusr
    toplazyBM
    writemoney
    reaper
    buildAnnounce
    mailangel
    outmail
    chkhbf
    angel
    gamblegive
    chesscountry
    tunepasswd
    buildir
    xchatd
    uhash_loader
    timecap_buildref
    showuser
    removebm
    redir
    permreport
    setrole
    update_online
    munin
    useractive_munin
    shmctl
    bbsmail
    )

set(CPP_WITH_UTIL
    mergedir2
    buildir2
    )

set(CPROG_WITHOUT_UTIL
    showboard
    bbsrf
    initbbs
    userlist
    merge_board
    bbsctl
    )

set(COPY_FILES
    backpasswd.sh
    mailog.sh
    openticket.sh
    topsong.sh
    weather.sh
    weather.perl
    toplazyBM.sh
    dailybackup.pl
    tarqueue.pl
    waterball.pl
    filtermail.pl
    getbackup.pl
    rebuildaloha.pl
    timecap_expire.sh
    )

add_custom_target(PROGS
    DEPENDS ${CPROG_WITH_UTIL} ${CPROG_WITHOUT_UTIL} ${CPP_WITH_UTIL} ${COPY_FILES}
    )

file(COPY ${COPY_FILES} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

foreach(src ${UTILSRCS})
    string(REGEX REPLACE "^util_" "" mbbsd_src ${src})
    add_custom_command(OUTPUT ${src}
        COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/mbbsd/${mbbsd_src} ${src}
        )
endforeach()

set_source_files_properties(${UTILSRCS}
    PROPERTIES
    GENERATED TRUE
    COMPILE_FLAGS -D_BBS_UTIL_C_
)

add_library(utilvar STATIC
    ${UTILSRCS}
    )

foreach(prog ${CPROG_WITH_UTIL})
    add_executable(${prog}
        ${prog}.c
        )
    target_link_libraries(${prog} ${LDLIBS} utilvar)
endforeach()

foreach(prog ${CPP_WITH_UTIL})
    add_executable(${prog}
        ${prog}.cc
        )
    target_link_libraries(${prog} ${LDLIBS} utilvar)
    set_source_files_properties(${prog}.cc
        PROPERTIES
        LANGUAGE CXX
        )
endforeach()

foreach(prog ${CPROGR_WITHOUT_UTIL})
    add_executable(${prog}
        ${prog}.c
        )
endforeach()