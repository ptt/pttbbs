googletest
==========

加上 googletest 做為 unit-test 的 framework.

file structure
----------

在 tests 裡. 有相對應於 source-code 的 file-structure:

    tests
        /test_common
            /test_bbs
                /test_banip.cc
            /test_osdep
                /test_strlcat.cc
            /test_sys
                /test_utf8.cc
        /test_mbbsd
            /test_bbs.cc


pttbbs.mk 系列
----------

增加以下 3 個 .mk 來幫助容易增加 Makefile:

1. pttbbs_cond.mk

    根據 pttbbs.conf 所得到的 condition 設定. 目前需要手動跟 mbbsd/Makefile 同步 update. 預期以後 mbbsd/Makefile 可以直接使用 pttbbs_cond.mk

2. pttbbs_mbbsd.mk

    mbbsd obj 相關的 .mk. 目前需要手動跟 mbbsd/Makefile 同步 update. 預期以後 mbbsd/Makefile 可以直接使用 pttbbs_mbbsd.mk


3. pttbbs_test.mk

    test 相關設定的 .mk


docker build
----------

我們利用 dockerfiles/Dockerfile.test 來作為建立 unit-test 環境的參考方式.
以下為利用 docker build 的 script 來做 unit-test:

    scripts/docker_build.sh test test test

如果想要再重新測試:

    docker run -itd --name test pttbbs:test /bin/bash
    docker exec -it test /bin/bash
    cd /home/bbs/pttbbs
    ./scripts/test_all.sh
    ./scripts/test_mem_leak_all.sh

