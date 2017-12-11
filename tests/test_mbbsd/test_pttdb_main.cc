#include "gtest/gtest.h"
#include "bbs.h"
#include "ptterr.h"
#include "pttdb.h"
#include "pttdb_internal.h"
#include "util_db_internal.h"

TEST(pttdb, create_main_from_fd_test1_read_main_content) {
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    int fd = open("data_test/test1.txt", O_RDONLY);

    aidu_t aid = 12345;
    char title[TTLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char origin[MAX_ORIGIN_LEN + 1] = {};
    char web_link[MAX_WEB_LINK_LEN + 1] = {};
    int len = 10000;
    UUID main_id;
    UUID content_id;

    char tmp_main_id[UUIDLEN + 1] = {};

    strcpy(title, "test_title");
    strcpy(poster, "test_poster");
    strcpy(ip, "test_ip");
    strcpy(origin, "ptt.cc");
    strcpy(web_link, "http://www.ptt.cc/bbs/alonglonglongboard/M.1234567890.ABCD.html");

    Err error_code = create_main_from_fd(aid, title, poster, ip, origin, web_link, len, fd, main_id, content_id);
    EXPECT_EQ(S_OK, error_code);

    close(fd);

    strncpy((char *)tmp_main_id, (char *)main_id, UUIDLEN);
    fprintf(stderr, "test_pttdb.create_main_from_fd: after create_main_from_fd: main_id: %s\n", tmp_main_id);

    MainHeader main_header = {};

    error_code = read_main_header(main_id, &main_header);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(0, strncmp((char *)main_id, (char *)main_header.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_id, (char *)main_header.content_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_id, (char *)main_header.update_content_id, UUIDLEN));
    EXPECT_EQ(aid, main_header.aid);
    EXPECT_EQ(LIVE_STATUS_ALIVE, main_header.status);

    EXPECT_STREQ(poster, main_header.status_updater);
    EXPECT_STREQ(ip, main_header.status_update_ip);

    EXPECT_STREQ(title, main_header.title);
    EXPECT_STREQ(poster, main_header.poster);
    EXPECT_STREQ(ip, main_header.ip);
    EXPECT_STREQ(poster, main_header.updater);
    EXPECT_STREQ(ip, main_header.update_ip);

    EXPECT_STREQ(origin, main_header.origin);
    EXPECT_STREQ(web_link, main_header.web_link);

    EXPECT_EQ(0, main_header.reset_karma);

    EXPECT_EQ(len, main_header.len_total);
    EXPECT_EQ(2, main_header.n_total_block);
    EXPECT_EQ(9, main_header.n_total_line);

    MainContent main_content0;
    MainContent main_content1;

    char *str_content = (char *)malloc(len);
    fd = open("data_test/test1.txt", O_RDONLY);
    read(fd, str_content, len);
    close(fd);

    error_code = read_main_content(main_header.content_id, 0, &main_content0);
    EXPECT_EQ(S_OK, error_code);

    error_code = read_main_content(main_header.content_id, 1, &main_content1);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(len, main_content0.len_block + main_content1.len_block);
    EXPECT_EQ(main_header.n_total_line, main_content0.n_line + main_content1.n_line);
    EXPECT_EQ(0, strncmp((char *)main_content0.buf_block, str_content, main_content0.len_block));
    EXPECT_EQ(0, strncmp((char *)main_content1.buf_block, str_content + main_content0.len_block, main_content1.len_block));

    free(str_content);
}


TEST(pttdb, create_main_from_fd_test1) {
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    int fd = open("data_test/test1.txt", O_RDONLY);

    aidu_t aid = 12345;
    char title[TTLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char origin[MAX_ORIGIN_LEN + 1] = {};
    char web_link[MAX_WEB_LINK_LEN + 1] = {};
    int len = 10000;
    UUID main_id;
    UUID content_id;

    char tmp_main_id[UUIDLEN + 1] = {};

    strcpy(title, "test_title");
    strcpy(poster, "test_poster");
    strcpy(ip, "test_ip");
    strcpy(origin, "ptt.cc");
    strcpy(web_link, "http://www.ptt.cc/bbs/alonglonglongboard/M.1234567890.ABCD.html");

    Err error_code = create_main_from_fd(aid, title, poster, ip, origin, web_link, len, fd, main_id, content_id);
    EXPECT_EQ(S_OK, error_code);

    strncpy((char *)tmp_main_id, (char *)main_id, UUIDLEN);
    fprintf(stderr, "test_pttdb.create_main_from_fd: after create_main_from_fd: main_id: %s\n", tmp_main_id);

    MainHeader main_header = {};

    error_code = read_main_header(main_id, &main_header);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(0, strncmp((char *)main_id, (char *)main_header.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_id, (char *)main_header.content_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_id, (char *)main_header.update_content_id, UUIDLEN));
    EXPECT_EQ(aid, main_header.aid);
    EXPECT_EQ(LIVE_STATUS_ALIVE, main_header.status);

    EXPECT_STREQ(poster, main_header.status_updater);
    EXPECT_STREQ(ip, main_header.status_update_ip);

    EXPECT_STREQ(title, main_header.title);
    EXPECT_STREQ(poster, main_header.poster);
    EXPECT_STREQ(ip, main_header.ip);
    EXPECT_STREQ(poster, main_header.updater);
    EXPECT_STREQ(ip, main_header.update_ip);

    EXPECT_STREQ(origin, main_header.origin);
    EXPECT_STREQ(web_link, main_header.web_link);

    EXPECT_EQ(0, main_header.reset_karma);

    EXPECT_EQ(len, main_header.len_total);
    EXPECT_EQ(2, main_header.n_total_block);
    EXPECT_EQ(9, main_header.n_total_line);

    close(fd);
}

TEST(pttdb, create_main_from_fd_test2_read_main_content) {
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    int fd = open("data_test/test2.txt", O_RDONLY);

    aidu_t aid = 12345;
    char title[TTLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char origin[MAX_ORIGIN_LEN + 1] = {};
    char web_link[MAX_WEB_LINK_LEN + 1] = {};
    int len = 10000;
    UUID main_id;
    UUID content_id;

    char tmp_main_id[UUIDLEN + 1] = {};

    strcpy(title, "test_title");
    strcpy(poster, "test_poster");
    strcpy(ip, "test_ip");
    strcpy(origin, "ptt.cc");
    strcpy(web_link, "http://www.ptt.cc/bbs/alonglonglongboard/M.1234567890.ABCD.html");

    Err error_code = create_main_from_fd(aid, title, poster, ip, origin, web_link, len, fd, main_id, content_id);
    EXPECT_EQ(S_OK, error_code);

    strncpy((char *)tmp_main_id, (char *)main_id, UUIDLEN);
    fprintf(stderr, "test_pttdb.create_main_from_fd: after create_main_from_fd: main_id: %s\n", tmp_main_id);

    MainHeader main_header = {};

    error_code = read_main_header(main_id, &main_header);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(0, strncmp((char *)main_id, (char *)main_header.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_header.content_id, (char *)main_header.update_content_id, UUIDLEN));
    EXPECT_EQ(aid, main_header.aid);
    EXPECT_EQ(LIVE_STATUS_ALIVE, main_header.status);

    EXPECT_STREQ(poster, main_header.status_updater);
    EXPECT_STREQ(ip, main_header.status_update_ip);

    EXPECT_STREQ(title, main_header.title);
    EXPECT_STREQ(poster, main_header.poster);
    EXPECT_STREQ(ip, main_header.ip);
    EXPECT_STREQ(poster, main_header.updater);
    EXPECT_STREQ(ip, main_header.update_ip);

    EXPECT_STREQ(origin, main_header.origin);
    EXPECT_STREQ(web_link, main_header.web_link);

    EXPECT_EQ(0, main_header.reset_karma);

    EXPECT_EQ(len, main_header.len_total);
    EXPECT_EQ(2, main_header.n_total_block);
    EXPECT_EQ(0, main_header.n_total_line);

    close(fd);

    MainContent main_content0;
    MainContent main_content1;

    char *str_content = (char *)malloc(len);
    fd = open("data_test/test2.txt", O_RDONLY);
    read(fd, str_content, len);
    close(fd);

    error_code = read_main_content(main_header.content_id, 0, &main_content0);
    EXPECT_EQ(S_OK, error_code);

    error_code = read_main_content(main_header.content_id, 1, &main_content1);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(len, main_content0.len_block + main_content1.len_block);
    EXPECT_EQ(main_header.n_total_line, main_content0.n_line + main_content1.n_line);
    EXPECT_EQ(0, strncmp((char *)main_content0.buf_block, str_content, main_content0.len_block));
    EXPECT_EQ(0, strncmp((char *)main_content1.buf_block, str_content + main_content0.len_block, main_content1.len_block));

    free(str_content);
}

TEST(pttdb, create_main_from_fd_test2) {
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    int fd = open("data_test/test2.txt", O_RDONLY);

    aidu_t aid = 12345;
    char title[TTLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char origin[MAX_ORIGIN_LEN + 1] = {};
    char web_link[MAX_WEB_LINK_LEN + 1] = {};
    int len = 10000;
    UUID main_id;
    UUID content_id;

    char tmp_main_id[UUIDLEN + 1] = {};

    strcpy(title, "test_title");
    strcpy(poster, "test_poster");
    strcpy(ip, "test_ip");
    strcpy(origin, "ptt.cc");
    strcpy(web_link, "http://www.ptt.cc/bbs/alonglonglongboard/M.1234567890.ABCD.html");

    Err error_code = create_main_from_fd(aid, title, poster, ip, origin, web_link, len, fd, main_id, content_id);
    EXPECT_EQ(S_OK, error_code);

    strncpy((char *)tmp_main_id, (char *)main_id, UUIDLEN);
    fprintf(stderr, "test_pttdb.create_main_from_fd: after create_main_from_fd: main_id: %s\n", tmp_main_id);

    MainHeader main_header = {};

    error_code = read_main_header(main_id, &main_header);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(0, strncmp((char *)main_id, (char *)main_header.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_header.content_id, (char *)main_header.update_content_id, UUIDLEN));
    EXPECT_EQ(aid, main_header.aid);
    EXPECT_EQ(LIVE_STATUS_ALIVE, main_header.status);

    EXPECT_STREQ(poster, main_header.status_updater);
    EXPECT_STREQ(ip, main_header.status_update_ip);

    EXPECT_STREQ(title, main_header.title);
    EXPECT_STREQ(poster, main_header.poster);
    EXPECT_STREQ(ip, main_header.ip);
    EXPECT_STREQ(poster, main_header.updater);
    EXPECT_STREQ(ip, main_header.update_ip);

    EXPECT_STREQ(origin, main_header.origin);
    EXPECT_STREQ(web_link, main_header.web_link);

    EXPECT_EQ(0, main_header.reset_karma);

    EXPECT_EQ(len, main_header.len_total);
    EXPECT_EQ(2, main_header.n_total_block);
    EXPECT_EQ(0, main_header.n_total_line);

    close(fd);
}

TEST(pttdb, create_main_from_fd_test1_full_read_main_content) {
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    int fd = open("data_test/test1.txt", O_RDONLY);

    time64_t start_timestamp;
    time64_t end_timestamp;

    aidu_t aid = 12345;
    char title[TTLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char origin[MAX_ORIGIN_LEN + 1] = {};
    char web_link[MAX_WEB_LINK_LEN + 1] = {};
    int len = 10020;
    UUID main_id;
    UUID content_id;

    char tmp_main_id[UUIDLEN + 1] = {};

    strcpy(title, "test_title");
    strcpy(poster, "test_poster");
    strcpy(ip, "test_ip");
    strcpy(origin, "ptt.cc");
    strcpy(web_link, "http://www.ptt.cc/bbs/alonglonglongboard/M.1234567890.ABCD.html");

    Err error_code = create_main_from_fd(aid, title, poster, ip, origin, web_link, len, fd, main_id, content_id);
    EXPECT_EQ(S_OK, error_code);

    strncpy((char *)tmp_main_id, (char *)main_id, UUIDLEN);
    fprintf(stderr, "test_pttdb.create_main_from_fd: after create_main_from_fd: main_id: %s\n", tmp_main_id);

    MainHeader main_header = {};

    get_milli_timestamp(&start_timestamp);

    error_code = read_main_header(main_id, &main_header);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(0, strncmp((char *)main_id, (char *)main_header.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_header.content_id, (char *)main_header.update_content_id, UUIDLEN));
    EXPECT_EQ(aid, main_header.aid);
    EXPECT_EQ(LIVE_STATUS_ALIVE, main_header.status);

    EXPECT_STREQ(poster, main_header.status_updater);
    EXPECT_STREQ(ip, main_header.status_update_ip);

    EXPECT_STREQ(title, main_header.title);
    EXPECT_STREQ(poster, main_header.poster);
    EXPECT_STREQ(ip, main_header.ip);
    EXPECT_STREQ(poster, main_header.updater);
    EXPECT_STREQ(ip, main_header.update_ip);

    EXPECT_STREQ(origin, main_header.origin);
    EXPECT_STREQ(web_link, main_header.web_link);

    EXPECT_EQ(0, main_header.reset_karma);

    EXPECT_EQ(len, main_header.len_total);
    EXPECT_EQ(2, main_header.n_total_block);
    EXPECT_EQ(10, main_header.n_total_line);

    close(fd);

    MainContent main_content0;
    MainContent main_content1;

    char *str_content = (char *)malloc(len);
    fd = open("data_test/test1.txt", O_RDONLY);
    read(fd, str_content, len);
    close(fd);

    error_code = read_main_content(main_header.content_id, 0, &main_content0);
    EXPECT_EQ(S_OK, error_code);

    error_code = read_main_content(main_header.content_id, 1, &main_content1);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(len, main_content0.len_block + main_content1.len_block);
    EXPECT_EQ(main_header.n_total_line, main_content0.n_line + main_content1.n_line);
    EXPECT_EQ(0, strncmp((char *)main_content0.buf_block, str_content, main_content0.len_block));
    EXPECT_EQ(0, strncmp((char *)main_content1.buf_block, str_content + main_content0.len_block, main_content1.len_block));

    get_milli_timestamp(&end_timestamp);

    fprintf(stderr, "pttdb.create_main_from_fd_test1_full_read_main_content: read: elapsed time: %lld\n", end_timestamp - start_timestamp);

    free(str_content);
}

TEST(pttdb, create_main_from_fd_test1_full) {
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    int fd = open("data_test/test1.txt", O_RDONLY);

    aidu_t aid = 12345;
    char title[TTLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char origin[MAX_ORIGIN_LEN + 1] = {};
    char web_link[MAX_WEB_LINK_LEN + 1] = {};
    int len = 10020;
    UUID main_id;
    UUID content_id;

    char tmp_main_id[UUIDLEN + 1] = {};

    strcpy(title, "test_title");
    strcpy(poster, "test_poster");
    strcpy(ip, "test_ip");
    strcpy(origin, "ptt.cc");
    strcpy(web_link, "http://www.ptt.cc/bbs/alonglonglongboard/M.1234567890.ABCD.html");

    Err error_code = create_main_from_fd(aid, title, poster, ip, origin, web_link, len, fd, main_id, content_id);
    EXPECT_EQ(S_OK, error_code);

    strncpy((char *)tmp_main_id, (char *)main_id, UUIDLEN);
    fprintf(stderr, "test_pttdb.create_main_from_fd: after create_main_from_fd: main_id: %s\n", tmp_main_id);

    MainHeader main_header = {};

    error_code = read_main_header(main_id, &main_header);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(0, strncmp((char *)main_id, (char *)main_header.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_header.content_id, (char *)main_header.update_content_id, UUIDLEN));
    EXPECT_EQ(aid, main_header.aid);
    EXPECT_EQ(LIVE_STATUS_ALIVE, main_header.status);

    EXPECT_STREQ(poster, main_header.status_updater);
    EXPECT_STREQ(ip, main_header.status_update_ip);

    EXPECT_STREQ(title, main_header.title);
    EXPECT_STREQ(poster, main_header.poster);
    EXPECT_STREQ(ip, main_header.ip);
    EXPECT_STREQ(poster, main_header.updater);
    EXPECT_STREQ(ip, main_header.update_ip);

    EXPECT_STREQ(origin, main_header.origin);
    EXPECT_STREQ(web_link, main_header.web_link);

    EXPECT_EQ(0, main_header.reset_karma);

    EXPECT_EQ(len, main_header.len_total);
    EXPECT_EQ(2, main_header.n_total_block);
    EXPECT_EQ(10, main_header.n_total_line);

    close(fd);
}

TEST(pttdb, len_main) {
    MainHeader main_header = {};

    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);

    gen_uuid(main_header.the_id);
    gen_uuid(main_header.content_id);
    gen_uuid(main_header.update_content_id);
    main_header.aid = 12345;
    main_header.status = LIVE_STATUS_ALIVE;
    strcpy(main_header.status_updater, "updater1");
    strcpy(main_header.status_update_ip, "10.1.1.1");
    strcpy(main_header.title, "test_title");
    strcpy(main_header.poster, "poster1");
    strcpy(main_header.ip, "10.1.1.2");
    main_header.create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST
    strcpy(main_header.updater, "updater2");
    strcpy(main_header.update_ip, "10.1.1.3");
    main_header.update_milli_timestamp = 1514764801000; //2018-01-01 08:00:01 CST
    strcpy(main_header.origin, "ptt.cc");
    strcpy(main_header.web_link, "https://www.ptt.cc/bbs/temp/M.1514764800.A.ABC.html");
    main_header.reset_karma = -100;
    main_header.n_total_line = 100;
    main_header.n_total_block = 20;
    main_header.len_total = 10000;

    bson_t main_bson;
    bson_init(&main_bson);

    Err error = _serialize_main_bson(&main_header, &main_bson);
    EXPECT_EQ(S_OK, error);

    error = db_update_one(MONGO_MAIN, &main_bson, &main_bson, true);
    EXPECT_EQ(S_OK, error);

    int len;
    error = len_main(main_header.the_id, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(main_header.len_total, len);
}

TEST(pttdb, len_main_by_aid) {
    MainHeader main_header = {};

    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);

    gen_uuid(main_header.the_id);
    gen_uuid(main_header.content_id);
    gen_uuid(main_header.update_content_id);
    main_header.aid = 12345;
    main_header.status = LIVE_STATUS_ALIVE;
    strcpy(main_header.status_updater, "updater1");
    strcpy(main_header.status_update_ip, "10.1.1.1");
    strcpy(main_header.title, "test_title");
    strcpy(main_header.poster, "poster1");
    strcpy(main_header.ip, "10.1.1.2");
    main_header.create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST
    strcpy(main_header.updater, "updater2");
    strcpy(main_header.update_ip, "10.1.1.3");
    main_header.update_milli_timestamp = 1514764801000; //2018-01-01 08:00:01 CST
    strcpy(main_header.origin, "ptt.cc");
    strcpy(main_header.web_link, "https://www.ptt.cc/bbs/temp/M.1514764800.A.ABC.html");
    main_header.reset_karma = -100;
    main_header.n_total_line = 100;
    main_header.n_total_block = 20;
    main_header.len_total = 10000;

    bson_t main_bson;
    bson_init(&main_bson);

    Err error = _serialize_main_bson(&main_header, &main_bson);
    EXPECT_EQ(S_OK, error);

    error = db_update_one(MONGO_MAIN, &main_bson, &main_bson, true);
    EXPECT_EQ(S_OK, error);

    int len;
    error = len_main_by_aid(main_header.aid, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(main_header.len_total, len);
}

TEST(pttdb, n_line_main) {
    MainHeader main_header = {};

    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);

    gen_uuid(main_header.the_id);
    gen_uuid(main_header.content_id);
    gen_uuid(main_header.update_content_id);
    main_header.aid = 12345;
    main_header.status = LIVE_STATUS_ALIVE;
    strcpy(main_header.status_updater, "updater1");
    strcpy(main_header.status_update_ip, "10.1.1.1");
    strcpy(main_header.title, "test_title");
    strcpy(main_header.poster, "poster1");
    strcpy(main_header.ip, "10.1.1.2");
    main_header.create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST
    strcpy(main_header.updater, "updater2");
    strcpy(main_header.update_ip, "10.1.1.3");
    main_header.update_milli_timestamp = 1514764801000; //2018-01-01 08:00:01 CST
    strcpy(main_header.origin, "ptt.cc");
    strcpy(main_header.web_link, "https://www.ptt.cc/bbs/temp/M.1514764800.A.ABC.html");
    main_header.reset_karma = -100;
    main_header.n_total_line = 100;
    main_header.n_total_block = 20;
    main_header.len_total = 10000;

    bson_t main_bson;
    bson_init(&main_bson);

    Err error = _serialize_main_bson(&main_header, &main_bson);
    EXPECT_EQ(S_OK, error);

    error = db_update_one(MONGO_MAIN, &main_bson, &main_bson, true);
    EXPECT_EQ(S_OK, error);

    int n_line;
    error = n_line_main(main_header.the_id, &n_line);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(main_header.n_total_line, n_line);
}

TEST(pttdb, n_line_main_by_aid) {
    MainHeader main_header = {};

    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);

    gen_uuid(main_header.the_id);
    gen_uuid(main_header.content_id);
    gen_uuid(main_header.update_content_id);
    main_header.aid = 12345;
    main_header.status = LIVE_STATUS_ALIVE;
    strcpy(main_header.status_updater, "updater1");
    strcpy(main_header.status_update_ip, "10.1.1.1");
    strcpy(main_header.title, "test_title");
    strcpy(main_header.poster, "poster1");
    strcpy(main_header.ip, "10.1.1.2");
    main_header.create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST
    strcpy(main_header.updater, "updater2");
    strcpy(main_header.update_ip, "10.1.1.3");
    main_header.update_milli_timestamp = 1514764801000; //2018-01-01 08:00:01 CST
    strcpy(main_header.origin, "ptt.cc");
    strcpy(main_header.web_link, "https://www.ptt.cc/bbs/temp/M.1514764800.A.ABC.html");
    main_header.reset_karma = -100;
    main_header.n_total_line = 100;
    main_header.n_total_block = 20;
    main_header.len_total = 10000;

    bson_t main_bson;
    bson_init(&main_bson);

    Err error = _serialize_main_bson(&main_header, &main_bson);
    EXPECT_EQ(S_OK, error);

    error = db_update_one(MONGO_MAIN, &main_bson, &main_bson, true);
    EXPECT_EQ(S_OK, error);

    int n_line;
    error = n_line_main_by_aid(main_header.aid, &n_line);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(main_header.n_total_line, n_line);
}

TEST(pttdb, read_main_header) {
    MainHeader main_header = {};
    MainHeader main_header2 = {};

    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);

    gen_uuid(main_header.the_id);
    gen_uuid(main_header.content_id);
    gen_uuid(main_header.update_content_id);
    main_header.aid = 12345;
    main_header.status = LIVE_STATUS_ALIVE;
    strcpy(main_header.status_updater, "updater1");
    strcpy(main_header.status_update_ip, "10.1.1.1");
    strcpy(main_header.title, "test_title");
    strcpy(main_header.poster, "poster1");
    strcpy(main_header.ip, "10.1.1.2");
    main_header.create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST
    strcpy(main_header.updater, "updater2");
    strcpy(main_header.update_ip, "10.1.1.3");
    main_header.update_milli_timestamp = 1514764801000; //2018-01-01 08:00:01 CST
    strcpy(main_header.origin, "ptt.cc");
    strcpy(main_header.web_link, "https://www.ptt.cc/bbs/temp/M.1514764800.A.ABC.html");
    main_header.reset_karma = -100;
    main_header.n_total_line = 100;
    main_header.n_total_block = 20;
    main_header.len_total = 10000;

    bson_t main_bson;
    bson_init(&main_bson);

    Err error = _serialize_main_bson(&main_header, &main_bson);
    EXPECT_EQ(S_OK, error);

    char *str = bson_as_canonical_extended_json(&main_bson, NULL);
    fprintf(stderr, "test_pttdb.read_main_header: to db_update_one: main_bson: %s\n", str);
    bson_free(str);

    error = db_update_one(MONGO_MAIN, &main_bson, &main_bson, true);
    EXPECT_EQ(S_OK, error);

    error = read_main_header(main_header.the_id, &main_header2);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(main_header.version, main_header2.version);
    EXPECT_EQ(0, strncmp((char *)main_header.the_id, (char *)main_header2.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_header.content_id, (char *)main_header2.content_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_header.update_content_id, (char *)main_header2.update_content_id, UUIDLEN));
    EXPECT_EQ(main_header.aid, main_header2.aid);
    EXPECT_EQ(main_header.status, main_header2.status);
    EXPECT_STREQ(main_header.status_updater, main_header2.status_updater);
    EXPECT_STREQ(main_header.status_update_ip, main_header2.status_update_ip);
    EXPECT_STREQ(main_header.title, main_header2.title);
    EXPECT_STREQ(main_header.poster, main_header2.poster);
    EXPECT_STREQ(main_header.ip, main_header2.ip);
    EXPECT_EQ(main_header.create_milli_timestamp, main_header2.create_milli_timestamp);
    EXPECT_STREQ(main_header.updater, main_header2.updater);
    EXPECT_STREQ(main_header.update_ip, main_header2.update_ip);
    EXPECT_EQ(main_header.update_milli_timestamp, main_header2.update_milli_timestamp);
    EXPECT_STREQ(main_header.origin, main_header2.origin);
    EXPECT_STREQ(main_header.web_link, main_header2.web_link);
    EXPECT_EQ(main_header.reset_karma, main_header2.reset_karma);
    EXPECT_EQ(main_header.n_total_line, main_header2.n_total_line);
    EXPECT_EQ(main_header.n_total_block, main_header2.n_total_block);
    EXPECT_EQ(main_header.len_total, main_header2.len_total);
}

TEST(pttdb, read_main_header_by_aid) {
    MainHeader main_header = {};
    MainHeader main_header2 = {};

    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);

    gen_uuid(main_header.the_id);
    gen_uuid(main_header.content_id);
    gen_uuid(main_header.update_content_id);
    main_header.aid = 12345;
    main_header.status = LIVE_STATUS_ALIVE;
    strcpy(main_header.status_updater, "updater1");
    strcpy(main_header.status_update_ip, "10.1.1.1");
    strcpy(main_header.title, "test_title");
    strcpy(main_header.poster, "poster1");
    strcpy(main_header.ip, "10.1.1.2");
    main_header.create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST
    strcpy(main_header.updater, "updater2");
    strcpy(main_header.update_ip, "10.1.1.3");
    main_header.update_milli_timestamp = 1514764801000; //2018-01-01 08:00:01 CST
    strcpy(main_header.origin, "ptt.cc");
    strcpy(main_header.web_link, "https://www.ptt.cc/bbs/temp/M.1514764800.A.ABC.html");
    main_header.reset_karma = -100;
    main_header.n_total_line = 100;
    main_header.n_total_block = 20;
    main_header.len_total = 10000;

    bson_t main_bson;
    bson_init(&main_bson);

    Err error = _serialize_main_bson(&main_header, &main_bson);
    EXPECT_EQ(S_OK, error);

    error = db_update_one(MONGO_MAIN, &main_bson, &main_bson, true);
    EXPECT_EQ(S_OK, error);

    error = read_main_header_by_aid(main_header.aid, &main_header2);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(main_header.version, main_header2.version);
    EXPECT_EQ(0, strncmp((char *)main_header.the_id, (char *)main_header2.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_header.content_id, (char *)main_header2.content_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_header.update_content_id, (char *)main_header2.update_content_id, UUIDLEN));
    EXPECT_EQ(main_header.aid, main_header2.aid);
    EXPECT_EQ(main_header.status, main_header2.status);
    EXPECT_STREQ(main_header.status_updater, main_header2.status_updater);
    EXPECT_STREQ(main_header.status_update_ip, main_header2.status_update_ip);
    EXPECT_STREQ(main_header.title, main_header2.title);
    EXPECT_STREQ(main_header.poster, main_header2.poster);
    EXPECT_STREQ(main_header.ip, main_header2.ip);
    EXPECT_EQ(main_header.create_milli_timestamp, main_header2.create_milli_timestamp);
    EXPECT_STREQ(main_header.updater, main_header2.updater);
    EXPECT_STREQ(main_header.update_ip, main_header2.update_ip);
    EXPECT_EQ(main_header.update_milli_timestamp, main_header2.update_milli_timestamp);
    EXPECT_STREQ(main_header.origin, main_header2.origin);
    EXPECT_STREQ(main_header.web_link, main_header2.web_link);
    EXPECT_EQ(main_header.reset_karma, main_header2.reset_karma);
    EXPECT_EQ(main_header.n_total_line, main_header2.n_total_line);
    EXPECT_EQ(main_header.n_total_block, main_header2.n_total_block);
    EXPECT_EQ(main_header.len_total, main_header2.len_total);
}

TEST(pttdb, read_main_content) {
    MainContent main_content_block = {};
    MainContent main_content_block2 = {};

    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);

    gen_uuid(main_content_block.the_id);
    gen_uuid(main_content_block.main_id);
    main_content_block.block_id = 53;
    main_content_block.len_block = 123;
    main_content_block.n_line = 2;
    strcpy(main_content_block.buf_block, "test123\n");

    bson_t b;
    bson_init(&b);

    Err error = _serialize_main_content_block_bson(&main_content_block, &b);
    EXPECT_EQ(S_OK, error);

    error = db_update_one(MONGO_MAIN_CONTENT, &b, &b, true);
    EXPECT_EQ(S_OK, error);

    bson_destroy(&b);

    error = read_main_content(main_content_block.the_id, main_content_block.block_id, &main_content_block2);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, strncmp((char *)main_content_block.the_id, (char *)main_content_block2.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_content_block.main_id, (char *)main_content_block2.main_id, UUIDLEN));
    EXPECT_EQ(main_content_block.block_id, main_content_block2.block_id);
    EXPECT_EQ(main_content_block.len_block, main_content_block2.len_block);
    EXPECT_EQ(main_content_block.n_line, main_content_block2.n_line);
    EXPECT_STREQ(main_content_block.buf_block, main_content_block2.buf_block);
}

TEST(pttdb, delete_main) {
    MainHeader main_header = {};

    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);

    gen_uuid(main_header.the_id);
    gen_uuid(main_header.content_id);
    gen_uuid(main_header.update_content_id);
    main_header.aid = 12345;
    main_header.status = LIVE_STATUS_ALIVE;
    strcpy(main_header.status_updater, "updater1");
    strcpy(main_header.status_update_ip, "10.1.1.1");
    strcpy(main_header.title, "test_title");
    strcpy(main_header.poster, "poster1");
    strcpy(main_header.ip, "10.1.1.2");
    main_header.create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST
    strcpy(main_header.updater, "updater2");
    strcpy(main_header.update_ip, "10.1.1.3");
    main_header.update_milli_timestamp = 1514764801000; //2018-01-01 08:00:01 CST
    strcpy(main_header.origin, "ptt.cc");
    strcpy(main_header.web_link, "https://www.ptt.cc/bbs/temp/M.1514764800.A.ABC.html");
    main_header.reset_karma = -100;
    main_header.n_total_line = 100;
    main_header.n_total_block = 20;
    main_header.len_total = 10000;

    bson_t main_bson;
    bson_init(&main_bson);

    Err error = _serialize_main_bson(&main_header, &main_bson);
    EXPECT_EQ(S_OK, error);

    error = db_update_one(MONGO_MAIN, &main_bson, &main_bson, true);
    EXPECT_EQ(S_OK, error);

    int len;
    char del_updater[IDLEN + 1] = "del_updater";
    char status_update_ip[IPV4LEN + 1] = "10.1.1.4";
    error = delete_main(main_header.the_id, del_updater, status_update_ip);
    EXPECT_EQ(S_OK, error);

    bson_t query;
    bson_t result;

    char **fields;
    int n_fields = 3;
    fields = (char **)malloc(sizeof(char *) * n_fields);
    for (int i = 0; i < 3; i++) {
        fields[i] = (char *)malloc(30);
    }
    strcpy(fields[0], "status");
    strcpy(fields[1], "status_updater");
    strcpy(fields[2], "status_update_ip");

    bson_init(&query);
    bson_init(&result);

    bson_append_bin(&query, "the_id", -1, main_header.the_id, UUIDLEN);

    error = db_find_one_with_fields(MONGO_MAIN, &query, fields, n_fields, &result);

    int result_status;
    char result_status_updater[MAX_BUF_SIZE];
    char result_status_update_ip[MAX_BUF_SIZE];
    bson_get_value_int32(&result, "status", &result_status);
    bson_get_value_bin(&result, "status_updater", MAX_BUF_SIZE, result_status_updater, &len);
    bson_get_value_bin(&result, "status_update_ip", MAX_BUF_SIZE, result_status_update_ip, &len);

    for (int i = 0; i < 3; i++) {
        free(fields[i]);
    }
    free(fields);

    EXPECT_EQ(LIVE_STATUS_DELETED, result_status);
    EXPECT_STREQ(del_updater, result_status_updater);
    EXPECT_STREQ(status_update_ip, result_status_update_ip);

    bson_destroy(&query);
    bson_destroy(&result);
}

TEST(pttdb, delete_main_by_aid) {
    MainHeader main_header = {};

    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);

    gen_uuid(main_header.the_id);
    gen_uuid(main_header.content_id);
    gen_uuid(main_header.update_content_id);
    main_header.aid = 12345;
    main_header.status = LIVE_STATUS_ALIVE;
    strcpy(main_header.status_updater, "updater1");
    strcpy(main_header.status_update_ip, "10.1.1.1");
    strcpy(main_header.title, "test_title");
    strcpy(main_header.poster, "poster1");
    strcpy(main_header.ip, "10.1.1.2");
    main_header.create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST
    strcpy(main_header.updater, "updater2");
    strcpy(main_header.update_ip, "10.1.1.3");
    main_header.update_milli_timestamp = 1514764801000; //2018-01-01 08:00:01 CST
    strcpy(main_header.origin, "ptt.cc");
    strcpy(main_header.web_link, "https://www.ptt.cc/bbs/temp/M.1514764800.A.ABC.html");
    main_header.reset_karma = -100;
    main_header.n_total_line = 100;
    main_header.n_total_block = 20;
    main_header.len_total = 10000;

    bson_t main_bson;
    bson_init(&main_bson);

    Err error = _serialize_main_bson(&main_header, &main_bson);
    EXPECT_EQ(S_OK, error);

    error = db_update_one(MONGO_MAIN, &main_bson, &main_bson, true);
    EXPECT_EQ(S_OK, error);

    int len;
    char del_updater[IDLEN + 1] = "del_updater";
    char status_update_ip[IPV4LEN + 1] = "10.1.1.4";
    error = delete_main_by_aid(main_header.aid, del_updater, status_update_ip);
    EXPECT_EQ(S_OK, error);

    bson_t query;
    bson_t result;

    char **fields;
    int n_fields = 3;
    fields = (char **)malloc(sizeof(char *) * n_fields);
    for (int i = 0; i < 3; i++) {
        fields[i] = (char *)malloc(30);
    }
    strcpy(fields[0], "status");
    strcpy(fields[1], "status_updater");
    strcpy(fields[2], "status_update_ip");

    bson_init(&query);
    bson_init(&result);

    bson_append_bin(&query, "the_id", -1, main_header.the_id, UUIDLEN);

    error = db_find_one_with_fields(MONGO_MAIN, &query, fields, n_fields, &result);

    int result_status;
    char result_status_updater[MAX_BUF_SIZE];
    char result_status_update_ip[MAX_BUF_SIZE];

    char *str = bson_as_canonical_extended_json(&result, NULL);
    fprintf(stderr, "test_pttdb.delete_main_by_aid: after db_find_one_with_fields: result: %s\n", str);
    bson_free(str);

    bson_get_value_int32(&result, "status", &result_status);
    bson_get_value_bin(&result, "status_updater", MAX_BUF_SIZE, result_status_updater, &len);
    bson_get_value_bin(&result, "status_update_ip", MAX_BUF_SIZE, result_status_update_ip, &len);

    for (int i = 0; i < 3; i++) {
        free(fields[i]);
    }
    free(fields);

    EXPECT_EQ(LIVE_STATUS_DELETED, result_status);
    EXPECT_STREQ(del_updater, result_status_updater);
    EXPECT_STREQ(status_update_ip, result_status_update_ip);

    bson_destroy(&query);
    bson_destroy(&result);
}

TEST(pttdb, serialize_main_bson) {
    MainHeader main_header = {};
    MainHeader main_header2 = {};

    main_header.version = 2;
    gen_uuid(main_header.the_id);
    gen_uuid(main_header.content_id);
    gen_uuid(main_header.update_content_id);
    main_header.aid = 12345;
    main_header.status = LIVE_STATUS_ALIVE;
    strcpy(main_header.status_updater, "updater1");
    strcpy(main_header.status_update_ip, "10.1.1.1");
    strcpy(main_header.title, "test_title");
    strcpy(main_header.poster, "poster1");
    strcpy(main_header.ip, "10.1.1.2");
    main_header.create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST
    strcpy(main_header.updater, "updater2");
    strcpy(main_header.update_ip, "10.1.1.3");
    main_header.update_milli_timestamp = 1514764801000; //2018-01-01 08:00:01 CST
    strcpy(main_header.origin, "ptt.cc");
    strcpy(main_header.web_link, "https://www.ptt.cc/bbs/temp/M.1514764800.A.ABC.html");
    main_header.reset_karma = -100;
    main_header.n_total_line = 100;
    main_header.n_total_block = 20;
    main_header.len_total = 10000;

    bson_t main_bson;
    bson_init(&main_bson);

    Err error = _serialize_main_bson(&main_header, &main_bson);
    EXPECT_EQ(S_OK, error);

    char *str = bson_as_canonical_extended_json(&main_bson, NULL);
    fprintf(stderr, "main_bson: %s\n", str);
    bson_free(str);

    error = _deserialize_main_bson(&main_bson, &main_header2);

    bson_destroy(&main_bson);

    fprintf(stderr, "main_header.the_id: %s main_header2.the_id: %s\n", main_header.the_id, main_header2.the_id);

    fprintf(stderr, "main_header.content_id: %s main_header2.content_id: %s\n", main_header.content_id, main_header2.content_id);

    fprintf(stderr, "main_header.status_update_ip: %s main_header2.status_update_ip: %s\n", main_header.status_update_ip, main_header2.status_update_ip);

    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(main_header.version, main_header2.version);
    EXPECT_EQ(0, strncmp((char *)main_header.the_id, (char *)main_header2.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_header.content_id, (char *)main_header2.content_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_header.update_content_id, (char *)main_header2.update_content_id, UUIDLEN));
    EXPECT_EQ(main_header.aid, main_header2.aid);
    EXPECT_EQ(main_header.status, main_header2.status);
    EXPECT_STREQ(main_header.status_updater, main_header2.status_updater);
    EXPECT_STREQ(main_header.status_update_ip, main_header2.status_update_ip);
    EXPECT_STREQ(main_header.title, main_header2.title);
    EXPECT_STREQ(main_header.poster, main_header2.poster);
    EXPECT_STREQ(main_header.ip, main_header2.ip);
    EXPECT_EQ(main_header.create_milli_timestamp, main_header2.create_milli_timestamp);
    EXPECT_STREQ(main_header.updater, main_header2.updater);
    EXPECT_STREQ(main_header.update_ip, main_header2.update_ip);
    EXPECT_EQ(main_header.update_milli_timestamp, main_header2.update_milli_timestamp);
    EXPECT_STREQ(main_header.origin, main_header2.origin);
    EXPECT_STREQ(main_header.web_link, main_header2.web_link);
    EXPECT_EQ(main_header.reset_karma, main_header2.reset_karma);
    EXPECT_EQ(main_header.n_total_line, main_header2.n_total_line);
    EXPECT_EQ(main_header.n_total_block, main_header2.n_total_block);
    EXPECT_EQ(main_header.len_total, main_header2.len_total);
}

TEST(pttdb, serialize_main_content_block_bson) {
    MainContent main_content_block = {};
    MainContent main_content_block2 = {};

    gen_uuid(main_content_block.the_id);
    gen_uuid(main_content_block.main_id);
    main_content_block.block_id = 53;
    main_content_block.len_block = 123;
    main_content_block.n_line = 2;
    strcpy(main_content_block.buf_block, "test123\n");

    bson_t b;
    bson_init(&b);

    Err error = _serialize_main_content_block_bson(&main_content_block, &b);
    EXPECT_EQ(S_OK, error);

    char *str = bson_as_canonical_extended_json(&b, NULL);
    fprintf(stderr, "b: %s\n", str);
    bson_free(str);

    error = _deserialize_main_content_block_bson(&b, &main_content_block2);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, strncmp((char *)main_content_block.the_id, (char *)main_content_block2.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)main_content_block.main_id, (char *)main_content_block2.main_id, UUIDLEN));
    EXPECT_EQ(main_content_block.block_id, main_content_block2.block_id);
    EXPECT_EQ(main_content_block.len_block, main_content_block2.len_block);
    EXPECT_EQ(main_content_block.n_line, main_content_block2.n_line);
    EXPECT_EQ(0, strncmp((char *)main_content_block.buf_block, (char *)main_content_block2.buf_block, MAX_BUF_SIZE));
}

TEST(pttdb, serialize_update_main_bson) {
    Err error_code;
    UUID content_id;
    char updater[IDLEN + 1] = {};
    char update_ip[IPV4LEN + 1] = {};
    time64_t update_milli_timestamp;
    int n_line;
    int n_block;
    int len;

    UUID result_content_id;
    char result_updater[IDLEN + 1] = {};
    char result_update_ip[IPV4LEN + 1] = {};
    time64_t result_update_milli_timestamp;
    int result_n_line;
    int result_n_block;
    int result_len;

    bool bson_status;

    strcpy(updater, "updater1");
    strcpy(update_ip, "10.1.1.5");
    get_milli_timestamp(&update_milli_timestamp);
    n_line = 103;
    n_block = 32;
    len = 2031;

    bson_t main_bson;
    bson_init(&main_bson);

    error_code = _serialize_update_main_bson(content_id, updater, update_ip, update_milli_timestamp, n_line, n_block, len, &main_bson);

    char *str = bson_as_canonical_extended_json(&main_bson, NULL);
    fprintf(stderr, "after serialize: str: %s\n", str);
    bson_free(str);

    int tmp;

    error_code = bson_get_value_bin(&main_bson, "content_id", UUIDLEN, (char *)result_content_id, &tmp);
    EXPECT_EQ(S_OK, error_code);

    error_code = bson_get_value_bin(&main_bson, "updater", IDLEN, result_updater, &tmp);
    EXPECT_EQ(S_OK, error_code);

    error_code = bson_get_value_bin(&main_bson, "update_ip", IPV4LEN, result_update_ip, &tmp);
    EXPECT_EQ(S_OK, error_code);

    error_code = bson_get_value_int64(&main_bson, "update_milli_timestamp", (long *)&result_update_milli_timestamp);
    EXPECT_EQ(S_OK, error_code);

    error_code = bson_get_value_int32(&main_bson, "n_total_line", &result_n_line);
    EXPECT_EQ(S_OK, error_code);

    error_code = bson_get_value_int32(&main_bson, "n_total_block", &result_n_block);
    EXPECT_EQ(S_OK, error_code);

    error_code = bson_get_value_int32(&main_bson, "len_total", &result_len);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(0, strncmp((char *)content_id, (char *)result_content_id, UUIDLEN));
    EXPECT_STREQ(updater, result_updater);
    EXPECT_STREQ(update_ip, result_update_ip);
    EXPECT_EQ(update_milli_timestamp, result_update_milli_timestamp);
    EXPECT_EQ(n_line, result_n_line);
    EXPECT_EQ(n_block, result_n_block);
    EXPECT_EQ(len, result_len);

    bson_destroy(&main_bson);

}

/**********
 * MAIN
 */
int FD = 0;

class MyEnvironment: public ::testing::Environment {
public:
    void SetUp();
    void TearDown();
};

void MyEnvironment::SetUp() {
    Err err = S_OK;

    FD = open("log.test_pttdb_main.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
    dup2(FD, 2);

    const char *db_name[] = {
        "test_post",
        "test",
    };

    err = init_mongo_global();
    if (err != S_OK) {
        fprintf(stderr, "[ERROR] UNABLE TO init mongo global\n");
        return;
    }
    err = init_mongo_collections(db_name);
    if (err != S_OK) {
        fprintf(stderr, "[ERROR] UNABLE TO init mongo collections\n");
        return;
    }

    FILE *f = fopen("data_test/test1.txt", "w");
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < 1000; i++) {
            fprintf(f, "%c", 64 + (i % 26));
        }
        fprintf(f, "\r\n");
    }
    fclose(f);

    f = fopen("data_test/test2.txt", "w");
    for (int i = 0; i < 10000; i++) {
        fprintf(f, "%c", 64 + (i % 26));
    }
    fclose(f);
}

void MyEnvironment::TearDown() {
    free_mongo_collections();
    free_mongo_global();

    if (FD) {
        close(FD);
        FD = 0;
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new MyEnvironment);

    return RUN_ALL_TESTS();
}
