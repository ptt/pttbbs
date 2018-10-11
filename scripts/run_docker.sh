#!/bin/bash

if [ "${BASH_ARGC}" != "1" ]
then
   echo "usage: run_docker.sh [tag]"
   exit 255
fi

tag=${BASH_ARGV[0]}

docker run --rm -itd -p 3000:3000 -p 3001:22 --name pttbbs pttbbs:${tag}
