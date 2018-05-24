#!/bin/bash

if [ "$#" -gt 3 ]
then
    echo "usage: docker_build.sh [[password]] [[tag]] [[tmpl]]"
    exit 255
fi

# variable setting
password='[TO_BE_REPLACED]'
tag=`git branch|grep '*'|awk '{print $2}'`
tmpl=default

if [ "$#" -ge 1 ]
then
    password=$1
fi

if [ "$#" -ge 2 ]
then
    tag=$2
fi

if [ "$#" -ge 3 ]
then
    tmpl=$3
fi

dockerfile="dockerfiles/Dockerfile.${tmpl}"

# run-commands
sed "s/\[TO_BE_REPLACED\]/${password}/g" "${dockerfile}" > "${dockerfile}.tmp"

docker build -t "pttbbs:${tag}" -f "${dockerfile}.tmp" .

rm "${dockerfile}.tmp"
