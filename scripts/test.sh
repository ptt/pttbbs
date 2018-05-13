#!/bin/bash

DIRS=( tests/test_common/test_bbs tests/test_common/test_osdep tests/test_common/test_sys tests/test_common/test_util_time tests/test_common/test_pttlib tests/test_common/test_pttdb tests/test_common/test_migrate_pttdb tests/test_common/test_pttui tests/test_mbbsd )

if [ "${BASH_ARGC}" != "1" ]
    then
    echo "usage: test [test-name]"
    exit 255
fi

test_name=${BASH_ARGV[0]}

for p in ${DIRS[*]}
do
    if [ -x "${p}/${test_name}" ]
        then
        echo -e "\e[33m${p}/${test_name}:\e[m"
        "${p}/${test_name}"
        echo ""
    fi
done
