#!/usr/bin/env bash

# expire rules
EXPIRE_MINUTES=+103000

# extract boards
BBSHOME=$HOME
BOARDS=$($BBSHOME/bin/showboard $BBSHOME/.BRD | sed 's/^ *[0-9][0-9]* //; s/ .*//')
BOARD_BASE=$BBSHOME/boards

# check TIME_CAPSULE_BASE_FOLDER_NAME in mbbsd/timecap.c
TIMECAP_NAME=".timecap"

i=0
num_boards=$(echo $BOARDS | wc -w)
date # for loggin
date >&2 # still for logging

for board in $BOARDS
do
    i=$((i + 1))
    timecap_base=$(
        printf "%s/%c/%s/%s" "$BOARD_BASE" "$board" "$board" "$TIMECAP_NAME")
    printf '\r%05d / %05d %-12s ...' $i $num_boards "$board" >&2
    if [ ! -d "$timecap_base" ]; then
        continue
    fi

    # first stage, expire files
    find "$timecap_base" -mmin "$EXPIRE_MINUTES" -ls -delete

    # second stage, modify .DIR files
    # dir_file="$timecap_base/archive.idx"
    $BBSHOME/bin/timecap_buildref "$timecap_base"
done
echo "" >&2
