使用 docker
==========

將 ptt containerize. 可以:

1. 讓 developers 容易知道相關的系統設定.
2. 可以放在 dockerhub 上. 讓大家可以直接 download 下來測試 / 使用.

使用方式
----------

目前提供以下的使用方式:

1. scripts/docker_build.sh [[password]] [[tag]]
   (default: password: '[TO_BE_REPLACED]'  tag: 目前的 branch)

   build docker (需先設定 pttbbs.conf)

2. scripts/run_docker.sh [[tag]]
   (default: tag: 目前的 branch)

   default run docker 的方式 (telnet localhost 3000, docker name 為 pttbbs:[tag])
