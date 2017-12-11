Unit-Test
===========

這個 unit-test 是 based on googletest. 在 ubuntu 裡使用 libgtest-dev.

Setup (在 ubuntu 16.04 裡)
-----
1. `apt-get install -y libgtest-dev`

2. 在 pttbbs 做 `make GTEST_DIR=/usr/src/gtest`

3. mkdir -p data_test

4. 執行相對應的 test (ex: tests/test_mbbsd/test_dummy)
