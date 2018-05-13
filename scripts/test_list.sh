#!/bin/bash

DIRS=( tests/test_common/test_bbs tests/test_common/test_osdep tests/test_common/test_sys tests/test_mbbsd )

for p in ${DIRS[*]}
do
    echo -e "\e[33m${p}:\e[m"
    for j in `ls ${p}/*`
    do
        if [ -x ${j} ]
            then
            k=`echo "${j}"|sed 's/.*\///g'`
            echo "${k}"
        fi
    done
    echo ""
done
