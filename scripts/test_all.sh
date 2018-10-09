#!/bin/bash

function traverse_dir {
    local dir=$1
    local each_file
    local ret=0
    for each_file in `ls ${dir}`
    do
        if [ "${each_file}" == "." -o "${each_file}" == ".." ]; then continue; fi

        if [ -d "${dir}/${each_file}" ]
        then
            traverse_dir "${dir}/${each_file}"
            ret=$?
            if [ "${ret}" != "0" ]; then return "${ret}"; fi
        elif [ -x "${dir}/${each_file}" ]
        then
            echo "${dir}/${each_file}:"
            "./${dir}/${each_file}" --gtest_color=yes
            ret=$?
            if [ "${ret}" != "0" ]; then return "${ret}"; fi
            echo ""
        fi
    done

    return 0
}

traverse_dir "tests"
