Build with CMake
==========

To build with CMake (in the root-dir of pttbbs):

1. cp sample/pttbbs.cmake.conf ./
2. mkdir -p build
3. cd build
4. cmake -DGTEST_DIR=/usr/src/gtest ..
5. make

Then you should be able to see the compiling process in the build dir.

You can do the following before the above steps for clang in Ubuntu:

`export CC=/usr/bin/clang; export CXX=/usr/bin/clang++`