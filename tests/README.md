Unit-Test
===========

這個 unit-test 是 based on googletest. 在 ubuntu 裡使用 libgtest-dev. 並使用 cmake 來做 test 相關的 compile. 需要先做 code 的 make, 再做 test 的 cmake.

Setup
-----
1. 在 ubuntu 裡:

    `apt-get install -y libgtest-dev`

2. 在 pttbbs 做 make

3. 在 pttbbs `mkdir build`

4. `cd build; cmake -DGTEST_DIR=/usr/src/googletest ..; make`

TODO
-----
1. 將 cmake 弄完整.

2. 考慮將 pttbbs 的 make 也整合進 cmake 裡.