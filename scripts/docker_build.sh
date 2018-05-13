#!/bin/bash

if [ "${BASH_ARGC}" != "1" ]
then
   echo "usage: docker_build.sh [tag]"
   exit 255
fi

tag=${BASH_ARGV[0]}

docker build --squash -t pttbbs:${tag} .
