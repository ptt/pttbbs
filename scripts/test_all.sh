#!/bin/bash

DIRS=( tests/test_common/test_bbs tests/test_common/test_osdep tests/test_common/test_sys tests/test_common/test_util_time tests/test_common/test_pttlib tests/test_common/test_pttdb tests/test_common/test_migrate_pttdb tests/test_common/test_pttui tests/test_mbbsd )

for p in ${DIRS[*]}
do
    for j in `ls ${p}/*`
    do
        if [ -x ${j} ]
            then
            echo -e "\e[33m${j}:\e[m"
            "${j}"
            ret="$?"
            if [ "${ret}" != "0" ]
                then
                echo "ERROR: ${ret}"
                exit 255
            fi
            echo ""
        fi
    done
done

exit 0