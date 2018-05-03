Unit-Test
===========

這個 unit-test 是 based on googletest. 在 ubuntu 裡使用 libgtest-dev.

Setup (在 ubuntu 16.04 裡)
-----
1. `apt-get install -y libgtest-dev valgrind`

2. 在 pttbbs 做 `make GTEST_DIR=/usr/src/gtest`

3. mkdir -p data_test

4. 執行相對應的 test (ex: tests/test_mbbsd/test_dummy)

5. 可執行 `scripts/test_all.sh` 執行所有的 tests

6. 可執行 `scripts/test_list.sh` list 所有的 tests

7. 可執行 `scripts/test.sh [test]` 執行 specific 的 test (ex: ./scripts/test.sh test_dummy)

8. 可執行 scripts/test_mem_leak_all.sh 來利用 valgrind check mem-leak
