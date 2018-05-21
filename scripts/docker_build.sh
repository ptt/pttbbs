#!/bin/bash

if [ "$#" -gt 2 ]
then
    echo "usage: docker_build.sh [[password]] [[tag]]"
    exit 255
fi

# variable setting
password='[TO_BE_REPLACED]'
tag=`git branch|grep '*'|awk '{print $2}'`

if [ "$#" -ge 1 ]
then
    password=$1
fi

if [ "$#" -ge 2 ]
then
    tag=$2
fi

dockerfile="dockerfiles/Dockerfile.default"

# run-commands
sed "s/\[TO_BE_REPLACED\]/${password}/g" "${dockerfile}" > "${dockerfile}.tmp"

docker build -t "pttbbs:${tag}" -f "${dockerfile}.tmp" .

rm "${dockerfile}.tmp"
