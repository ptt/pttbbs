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
            k=`valgrind --log-fd=1 --leak-check=yes "./${dir}/${each_file}"`
            d_lost_bytes=`echo "${k}"|grep "definitely lost"|awk '{print $4}'`
            d_lost_blocks=`echo "${k}"|grep "definitely lost"|awk '{print $7}'`
            id_lost_bytes=`echo "${k}"|grep "indirectly lost"|awk '{print $4}'`
            id_lost_blocks=`echo "${k}"|grep "indirectly lost"|awk '{print $7}'`
            p_lost_bytes=`echo "${k}"|grep "possibly lost"|awk '{print $4}'`
            p_lost_blocks=`echo "${k}"|grep "possibly lost"|awk '{print $7}'`
            t_lost_bytes=`expr "${d_lost_bytes}" + "${id_lost_bytes}" + "${p_lost_bytes}"`
            t_lost_blocks=`expr "${d_lost_blocks}" + "${id_lost_blocks}" + "${p_lost_blocks}"`

            echo "lost bytes: ${t_lost_bytes} lost blocks: ${t_lost_blocks}"

            if [ "${t_lost_bytes}" != "0" ]
            then
                echo "${k}"
                echo -e "\e[41m[  ERROR  ]\e[m"
                return -1
            fi
            echo ""
        fi
    done

    return 0
}

traverse_dir "tests"
if [ "$?" == "0" ]
then
    echo -e "\e[32m[  PASSED  ]\e[m"
fi
