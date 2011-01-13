#!/usr/bin/env bash

# expire rules
EXPIRE_MINUTES="+$((103000 / 3))"
HOME_EXPIRE_MINUTES="+$((103000 / 6))"

# configuration
BBSHOME=$HOME
BOARD_BASE="$BBSHOME/boards"
HOME_BASE="$BBSHOME/home"

# check TIME_CAPSULE_BASE_FOLDER_NAME in mbbsd/timecap.c
TIMECAP_NAME=".timecap"

expire_boards() {
    local BOARDS="$($BBSHOME/bin/showboard $BBSHOME/.BRD |
                    sed 's/^ *[0-9][0-9]* //; s/ .*//')"
    local board
    local num_boards="$(echo $BOARDS | wc -w)"
    local i=0
    local timecap_base

    for board in $BOARDS
    do
        i=$((i + 1))
        printf '\r%05d / %05d [B] %-12s ...' $i $num_boards "$board" >&2
        timecap_base=$(
            printf "%s/%c/%s/%s" "$BOARD_BASE" "$board" "$board" "$TIMECAP_NAME")
        if [ ! -d "$timecap_base" ]; then
            continue
        fi

        # first stage, expire files
        find "$timecap_base" -mmin "$EXPIRE_MINUTES" -ls -delete

        # second stage, modify .DIR files
        # dir_file="$timecap_base/archive.idx"
        $BBSHOME/bin/timecap_buildref "$timecap_base"
    done
}

expire_home() {
    local userid
    local i=0
    local timecap_base

    $BBSHOME/bin/showuser $BBSHOME/.PASSWD |
    while read userid
    do
        i=$((i + 1))
        printf '\r%07d [U] %-12s ...' $i "$userid" >&2
        timecap_base=$(
            printf "%s/%c/%s/%s" "$HOME_BASE" "$userid" "$userid" "$TIMECAP_NAME")
        if [ ! -d "$timecap_base" ]; then
            continue
        fi

        # first stage, expire files
        find "$timecap_base" -mmin "$HOME_EXPIRE_MINUTES" -ls -delete

        # second stage, modify .DIR files
        # dir_file="$timecap_base/archive.idx"
        $BBSHOME/bin/timecap_buildref "$timecap_base"
    done
}

date # for loggin
date >&2 # still for logging
expire_boards
expire_home
echo "" >&2
