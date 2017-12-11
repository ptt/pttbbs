Mongodb
===========

目前使用 Mongodb 3.6.1 與 mongo-c-driver 1.9.0.

Setup (在 ubuntu 16.04 裡)
-----
1. sudo apt-get install pkg-config libssl-dev libsasl2-dev
2. (libbson, should be in /usr/local/include and /usr/local/lib)
```
$ wget https://github.com/mongodb/libbson/releases/download/1.9.0/libbson-1.9.0.tar.gz
$ cd libbson-1.9.0
$ ./configure
$ make
$ sudo make install
```
3. (libmongoc, should be in /usr/local/include and /usr/local/lib)
```
$ wget https://github.com/mongodb/mongo-c-driver/releases/download/1.9.0/mongo-c-driver-1.9.0.tar.gz
$ tar xzf mongo-c-driver-1.9.0.tar.gz
$ cd mongo-c-driver-1.9.0
$ ./configure --disable-automatic-init-and-cleanup
$ make
$ sudo make install
```
4. (MongoDB 3.6.1)
```
$ wget https://www.mongodb.org/static/pgp/server-3.6.asc?_ga=2.166282174.1683259271.1515240542-1836212786.1508650053&_gac=1.188702170.1514872595.Cj0KCQiA1afSBRD2ARIsAEvBsNnIeAsyFiSl05B2C2j8-2cOqs8Yza46dFczuxE8cmZuo0QcpaNaqxwaAk5JEALw_wcB -O mongo-key.gpg
$ sudo apt-key add mongo-key.gpg
$ echo "deb [ arch=amd64,arm64 ] https://repo.mongodb.org/apt/ubuntu xenial/mongodb-org/3.6 multiverse" | sudo tee /etc/apt/sources.list.d/mongodb-org-3.6.list
$ sudo apt-get update
$ sudo apt-get install -y mongodb-org
```
5. (do tests/test_mbbsd/test_pttdb to pass test)
```
$ (uncomment MONGO_CLIENT_URL in pttbbs.conf)
$ make GTEST_DIR=/usr/src/gtest
$ ./tests/test_mbbsd/test_pttdb
```

Reference
-----
1. [libmongc](http://mongoc.org/libmongoc/current/index.html)
2. [libbson](http://mongoc.org/libbson/current/index.html)
3. [MongoDB](https://docs.mongodb.com/manual/tutorial/install-mongodb-on-ubuntu/)