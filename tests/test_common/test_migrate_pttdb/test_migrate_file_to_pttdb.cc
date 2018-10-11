#include "gtest/gtest.h"
#include "test.h"
#include "cmpttdb.h"
#include "cmmigrate_pttdb/migrate_file_to_pttdb.h"
#include "cmmigrate_pttdb/migrate_file_to_pttdb_private.h"
#include "cmmigrate_pttdb/migrate_pttdb_to_file.h"

TEST(migrate_file_to_pttdb, migrate_file_to_pttdb) {
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    EXPECT_EQ(S_OK, error);

    error = migrate_pttdb_to_file(main_id, "data_test/original_msg.4.txt.migrate");

    EXPECT_EQ(S_OK, error);
}


TEST(migrate_file_to_pttdb, parse_create_milli_timestamp_from_web_link) {
    char web_link[] = "https://www.ptt.cc/bbs/SYSOP/M.1510537375.A.8B4.html";

    time64_t create_milli_timestamp = 0;
    Err error = _parse_create_milli_timestamp_from_web_link(web_link, &create_milli_timestamp);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1510537375000, create_milli_timestamp);
}

TEST(migrate_file_to_pttdb, parse_create_milli_timestamp_from_filename) {
    char filename[] = "M.1510537375.A.8B4";
    time64_t create_milli_timestamp = 0;
    Err error =  _parse_create_milli_timestamp_from_filename(filename, &create_milli_timestamp);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1510537375000, create_milli_timestamp);
}

TEST(migrate_file_to_pttdb, is_comment_line_good_bad_arrow_invalid) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "testtest\r\n");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line_good_bad_arrow(line, MAX_BUF_SIZE, &is_valid, COMMENT_TYPE_GOOD);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_valid);
}

TEST(migrate_file_to_pttdb, is_comment_line_good_bad_arrow_invalid_n_only) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "testtest\n");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line_good_bad_arrow(line, MAX_BUF_SIZE, &is_valid, COMMENT_TYPE_GOOD);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_valid);
}

TEST(migrate_file_to_pttdb, is_comment_line_good_bad_arrow_good) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_GOOD], COMMENT_TYPE_ATTR[COMMENT_TYPE_GOOD], "poster001", 80, "test-msg", "02/31 03:31");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line_good_bad_arrow(line, MAX_BUF_SIZE, &is_valid, COMMENT_TYPE_GOOD);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_valid);
}

TEST(migrate_file_to_pttdb, is_comment_line_good_bad_arrow_bad) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_BAD], COMMENT_TYPE_ATTR[COMMENT_TYPE_BAD], "poster001", 80, "test-msg", "02/31 03:31");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line_good_bad_arrow(line, MAX_BUF_SIZE, &is_valid, COMMENT_TYPE_BAD);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_valid);
}

TEST(migrate_file_to_pttdb, is_comment_line_good_bad_arrow_arrow) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_ARROW], COMMENT_TYPE_ATTR[COMMENT_TYPE_ARROW], "poster001", 80, "test-msg", "02/31 03:31");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line_good_bad_arrow(line, MAX_BUF_SIZE, &is_valid, COMMENT_TYPE_ARROW);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_valid);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_is_line_edit) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s (%s), %s\n", LINE_EDIT_PREFIX, "poster001", "127.0.0.1", "02/27/2018 03:34:34");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_line_edit(line, MAX_BUF_SIZE, &is_valid);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_valid);
}

TEST(migrate_file_to_pttdb, is_comment_line_cross) {
    // bbs.c line: 2255 cross_post()
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s " ANSI_COLOR(1;32) "%s" ANSI_COLOR(0;32) COMMENT_CROSS_POST_PREFIX "%s" ANSI_RESET "%*s%s\n", COMMENT_TYPE_ATTR[COMMENT_TYPE_CROSS_POST], "poster001", COMMENT_CROSS_POST_HIDDEN_BOARD, 80, "", "02/31 03:31");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line_cross_post(line, MAX_BUF_SIZE, &is_valid);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_valid);
}

TEST(migrate_file_to_pttdb, is_comment_line_cross_invalid) {
    // bbs.c line: 2255 cross_post()
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_ARROW], COMMENT_TYPE_ATTR[COMMENT_TYPE_ARROW], "poster001", 80, "test-msg", "02/31 03:31");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line_cross_post(line, MAX_BUF_SIZE, &is_valid);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_valid);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_is_comment_line_reset_karma) {
    // bbs.c line: 2255 cross_post()
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s%s%s%s\r\n", COMMENT_TYPE_ATTR[COMMENT_TYPE_RESET_KARMA], "poster001", COMMENT_RESET_KARMA_INFIX, "02/27/2018 10:30:45", COMMENT_RESET_KARMA_POSTFIX);

    fprintf(stderr, "test_migrate_file_to_pttdb.is_comment_line_reset_karma: line: %s\n", line);

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line_reset_karma(line, MAX_BUF_SIZE, &is_valid);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_valid);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_is_comment_line_reset_karma_n_only) {
    // bbs.c line: 2255 cross_post()
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s%s%s%s\n", COMMENT_TYPE_ATTR[COMMENT_TYPE_RESET_KARMA], "poster001", COMMENT_RESET_KARMA_INFIX, "02/27/2018 10:30:45", COMMENT_RESET_KARMA_POSTFIX);

    fprintf(stderr, "test_migrate_file_to_pttdb.is_comment_line_reset: line: %s\n", line);

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line_reset_karma(line, MAX_BUF_SIZE, &is_valid);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_valid);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_is_comment_line_invalid) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "testtest\r\n");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line(line, MAX_BUF_SIZE, &is_valid);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_valid);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_is_comment_line_invalid_n_only) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "testtest\n");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line(line, MAX_BUF_SIZE, &is_valid);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_valid);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_is_comment_line_good) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_GOOD], COMMENT_TYPE_ATTR[COMMENT_TYPE_GOOD], "poster001", 80, "test-msg", "02/31");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line(line, MAX_BUF_SIZE, &is_valid);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_valid);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_is_comment_line_bad) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_BAD], COMMENT_TYPE_ATTR[COMMENT_TYPE_BAD], "poster001", 80, "test-msg", "02/31");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line(line, MAX_BUF_SIZE, &is_valid);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_valid);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_is_comment_line_arrow) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_ARROW], COMMENT_TYPE_ATTR[COMMENT_TYPE_ARROW], "poster001", 80, "test-msg", "02/31");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line(line, MAX_BUF_SIZE, &is_valid);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_valid);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_is_comment_line_cross_post2) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s " ANSI_COLOR(1;32) "%s" ANSI_COLOR(0;32) COMMENT_CROSS_POST_PREFIX "%s" ANSI_RESET "%*s%s\n", COMMENT_TYPE_ATTR[COMMENT_TYPE_CROSS_POST], "poster001", COMMENT_CROSS_POST_HIDDEN_BOARD, 80, "", "02/31");

    bool is_valid = false;
    Err error = _parse_legacy_file_is_comment_line(line, MAX_BUF_SIZE, &is_valid);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_valid);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_create_milli_timestamp_get_datetime_from_line) {

    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\r\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_GOOD], COMMENT_TYPE_ATTR[COMMENT_TYPE_GOOD], "poster001", 80, "test-msg", "02/27 03:03");

    int mm = 0;
    int dd = 0;
    int HH = 0;
    int MM = 0;

    int bytes_in_line = strlen(line);

    Err error = _parse_legacy_file_comment_create_milli_timestamp_get_datetime_from_line(line, bytes_in_line, &mm, &dd, &HH, &MM);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2, mm);
    EXPECT_EQ(27, dd);
    EXPECT_EQ(3, HH);
    EXPECT_EQ(3, MM);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_create_milli_timestamp_get_datetime_from_line_n_only) {

    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_GOOD], COMMENT_TYPE_ATTR[COMMENT_TYPE_GOOD], "poster001", 80, "test-msg", "02/27 03:03");

    int mm = 0;
    int dd = 0;
    int HH = 0;
    int MM = 0;

    int bytes_in_line = strlen(line);

    Err error = _parse_legacy_file_comment_create_milli_timestamp_get_datetime_from_line(line, bytes_in_line, &mm, &dd, &HH, &MM);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2, mm);
    EXPECT_EQ(27, dd);
    EXPECT_EQ(3, HH);
    EXPECT_EQ(3, MM);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_create_milli_timestamp_good_bad_arrow_cross_post_good) {
    time64_t current_create_milli_timestamp = 1520315640000; // 2018-03-06 13:54:00 (CST)

    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\r\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_GOOD], COMMENT_TYPE_ATTR[COMMENT_TYPE_GOOD], "poster001", 80, "test-msg", "02/27 03:03");

    int bytes_in_line = strlen(line);
    time64_t create_milli_timestamp = 0;
    Err error = _parse_legacy_file_comment_create_milli_timestamp_good_bad_arrow_cross_post(line, bytes_in_line, current_create_milli_timestamp, &create_milli_timestamp);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1551207780000, create_milli_timestamp); // 2019-02-27 03:03 (CST)

    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\r\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_GOOD], COMMENT_TYPE_ATTR[COMMENT_TYPE_GOOD], "poster001", 80, "test-msg", "03/06 13:54");

    error = _parse_legacy_file_comment_create_milli_timestamp_good_bad_arrow_cross_post(line, bytes_in_line, current_create_milli_timestamp, &create_milli_timestamp);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1520315640001, create_milli_timestamp); // 2018-03-06 13:54:00.001 (CST)
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_create_milli_timestamp_good_bad_arrow_cross_post_good_n_only) {
    time64_t current_create_milli_timestamp = 1520315640000; // 2018-03-06 13:54:00 (CST)

    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_GOOD], COMMENT_TYPE_ATTR[COMMENT_TYPE_GOOD], "poster001", 80, "test-msg", "02/27 03:03");

    int bytes_in_line = strlen(line);
    time64_t create_milli_timestamp = 0;
    Err error = _parse_legacy_file_comment_create_milli_timestamp_good_bad_arrow_cross_post(line, bytes_in_line, current_create_milli_timestamp, &create_milli_timestamp);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1551207780000, create_milli_timestamp); // 2019-02-27 03:03 (CST)

    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_GOOD], COMMENT_TYPE_ATTR[COMMENT_TYPE_GOOD], "poster001", 80, "test-msg", "03/06 13:54");

    error = _parse_legacy_file_comment_create_milli_timestamp_good_bad_arrow_cross_post(line, bytes_in_line, current_create_milli_timestamp, &create_milli_timestamp);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1520315640001, create_milli_timestamp); // 2018-03-06 13:54:00.001 (CST)
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_create_milli_timestamp_reset_karma) {

    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s%s%s%s\r\n", COMMENT_TYPE_ATTR[COMMENT_TYPE_RESET_KARMA], "poster001", COMMENT_RESET_KARMA_INFIX, "02/27/2018 10:30:45", COMMENT_RESET_KARMA_POSTFIX);

    time64_t create_milli_timestamp = 0;
    Err error = _parse_legacy_file_comment_create_milli_timestamp_reset_karma(line, MAX_BUF_SIZE, 1519698643000, &create_milli_timestamp);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1519698645000, create_milli_timestamp);    

    create_milli_timestamp = 0;
    error = _parse_legacy_file_comment_create_milli_timestamp_reset_karma(line, MAX_BUF_SIZE, 1519698646000, &create_milli_timestamp);
    EXPECT_EQ(S_ERR, error);
    EXPECT_EQ(0, create_milli_timestamp);    
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_create_milli_timestamp_reset_karma_n_only) {

    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s%s%s%s\n", COMMENT_TYPE_ATTR[COMMENT_TYPE_RESET_KARMA], "poster001", COMMENT_RESET_KARMA_INFIX, "02/27/2018 10:30:45", COMMENT_RESET_KARMA_POSTFIX);

    time64_t create_milli_timestamp = 0;
    Err error = _parse_legacy_file_comment_create_milli_timestamp_reset_karma(line, MAX_BUF_SIZE, 1519698643000, &create_milli_timestamp);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1519698645000, create_milli_timestamp);    

    create_milli_timestamp = 0;
    error = _parse_legacy_file_comment_create_milli_timestamp_reset_karma(line, MAX_BUF_SIZE, 1519698646000, &create_milli_timestamp);
    EXPECT_EQ(S_ERR, error);
    EXPECT_EQ(0, create_milli_timestamp);    
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_poster_good_bad_arrow_good) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_GOOD], COMMENT_TYPE_ATTR[COMMENT_TYPE_GOOD], "poster001", 80, "test-msg", "02/31");

    char poster[IDLEN + 1] = {};
    Err error = _parse_legacy_file_comment_poster_good_bad_arrow(line, MAX_BUF_SIZE, poster);
    EXPECT_EQ(S_OK, error);
    EXPECT_STREQ("poster001", poster);    
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_poster_good_bad_arrow_bad) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_BAD], COMMENT_TYPE_ATTR[COMMENT_TYPE_BAD], "poster001", 80, "test-msg", "02/31");

    char poster[IDLEN + 1] = {};
    Err error = _parse_legacy_file_comment_poster_good_bad_arrow(line, MAX_BUF_SIZE, poster);
    EXPECT_EQ(S_OK, error);
    EXPECT_STREQ("poster001", poster);    
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_poster_good_bad_arrow_arrow) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33) ":%-*s" ANSI_RESET "%s\n", COMMENT_TYPE_ATTR2[COMMENT_TYPE_ARROW], COMMENT_TYPE_ATTR[COMMENT_TYPE_ARROW], "poster001", 80, "test-msg", "02/31");

    char poster[IDLEN + 1] = {};
    Err error = _parse_legacy_file_comment_poster_good_bad_arrow(line, MAX_BUF_SIZE, poster);
    EXPECT_EQ(S_OK, error);
    EXPECT_STREQ("poster001", poster);    
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_poster_cross_post) {
    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s " ANSI_COLOR(1;32) "%s" ANSI_COLOR(0;32) COMMENT_CROSS_POST_PREFIX "%s" ANSI_RESET "%*s%s\n", COMMENT_TYPE_ATTR[COMMENT_TYPE_CROSS_POST], "poster001", COMMENT_CROSS_POST_HIDDEN_BOARD, 80, "", "02/31");

    char poster[IDLEN + 1] = {};
    Err error = _parse_legacy_file_comment_poster_cross_post(line, MAX_BUF_SIZE, poster);
    EXPECT_EQ(S_OK, error);
    EXPECT_STREQ("poster001", poster);    
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_poster_reset_karma) {

    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s%s%s%s\r\n", COMMENT_TYPE_ATTR[COMMENT_TYPE_RESET_KARMA], "poster001", COMMENT_RESET_KARMA_INFIX, "02/27/2018 10:30:45", COMMENT_RESET_KARMA_POSTFIX);

    char poster[IDLEN + 1] = {};
    Err error = _parse_legacy_file_comment_poster_reset_karma(line, MAX_BUF_SIZE, poster);
    EXPECT_EQ(S_OK, error);
    EXPECT_STREQ("poster001", poster);    
}

TEST(migrate_file_to_pttdb, parse_legacy_file_comment_poster_reset_karma_n_only) {

    char line[MAX_BUF_SIZE] = {};
    sprintf(line, "%s%s%s%s%s\n", COMMENT_TYPE_ATTR[COMMENT_TYPE_RESET_KARMA], "poster001", COMMENT_RESET_KARMA_INFIX, "02/27/2018 10:30:45", COMMENT_RESET_KARMA_POSTFIX);

    char poster[IDLEN + 1] = {};
    Err error = _parse_legacy_file_comment_poster_reset_karma(line, MAX_BUF_SIZE, poster);
    EXPECT_EQ(S_OK, error);
    EXPECT_STREQ("poster001", poster);    
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_1) {
    // M.1510537375.A.8B4
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.1.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.1.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1780, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.1.txt", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb._parse_legacy_file_main_info_1: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_1) {
    // M.1510537375.A.8B4
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.1.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.1.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1780, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.1.txt", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(4, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_1_n_only) {
    // M.1510537375.A.8B4
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.1.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.1.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1727, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.1.txt", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb._parse_legacy_file_main_info_1: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_1_n_only) {
    // M.1510537375.A.8B4
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.1.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.1.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1727, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.1.txt_n_only", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(4, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_2) {
    // M.1500464247.A.6AA
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.2.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.2.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(109216, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.2.txt", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_2: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_2) {
    // M.1510537375.A.8B4
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.2.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.2.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(109216, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.2.txt", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(15, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_2_n_only) {
    // M.1500464247.A.6AA
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.2.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.2.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(107092, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.2.txt_n_only", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_2: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_2_n_only) {
    // M.1510537375.A.8B4
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.2.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.2.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(107092, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.2.txt_n_only", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(15, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_3) {
    // M.1503755396.A.49D
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.3.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.3.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(6447, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.3.txt", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_3: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_3) {
    // M.1503755396.A.49D
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.3.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.3.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(6447, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.3.txt", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(79, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_3_n_only) {
    // M.1503755396.A.49D
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.3.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.3.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(6375, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.3.txt_n_only", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_3: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_3_n_only) {
    // M.1503755396.A.49D
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.3.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.3.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(6375, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.3.txt_n_only", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(79, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_4) {
    // M.1511576360.A.A15
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.4.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.4.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1310, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.4.txt", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_4: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_4) {
    // M.1511576360.A.A15
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.4.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.4.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1310, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.4.txt", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(103, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_4_n_only) {
    // M.1511576360.A.A15
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.4.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.4.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1268, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.4.txt_n_only", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_4: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_4_n_only) {
    // M.1511576360.A.A15
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.4.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.4.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1268, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.4.txt_n_only", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(103, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_5) {
    // M.997843374.A
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.5.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.5.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(653, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.5.txt", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_5: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_5) {
    // M.997843374.A
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.5.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.5.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(653, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.5.txt", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(0, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_5_n_only) {
    // M.997843374.A
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.5.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.5.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(630, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.5.txt_n_only", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_5: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_5_n_only) {
    // M.997843374.A
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.5.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.5.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(630, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.5.txt_n_only", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(0, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_6) {
    // M.997841455.A
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.6.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.6.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(779, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.6.txt", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_6: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_6) {
    // M.997841455.A
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.6.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.6.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(779, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.6.txt", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_6_n_only) {
    // M.997841455.A
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.6.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.6.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(755, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.6.txt_n_only", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_6: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_6_n_only) {
    // M.997841455.A
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.6.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.6.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(755, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.6.txt_n_only", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_7) {
    // M.997841455.A

    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.7.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.7.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(779, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.7.txt", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_7: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_7) {
    // M.997841455.A

    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.7.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.7.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(779, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.7.txt", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(0, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_7_n_only) {
    // M.997841455.A

    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.7.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.7.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(755, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.7.txt_n_only", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_7: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_7_n_only) {
    // M.997841455.A

    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.7.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.7.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(755, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.7.txt_n_only", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(0, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_8) {
    // M.1041489119.A.C28
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.8.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.8.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1993, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.8.txt", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_8: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_8) {
    // M.997841455.A

    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.8.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.8.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1993, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.8.txt", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(77, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_8_n_only) {
    // M.1041489119.A.C28
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.8.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.8.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1938, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.8.txt_n_only", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_8: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_8_n_only) {
    // M.997841455.A

    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.8.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.8.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1938, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.8.txt_n_only", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(77, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_9) {
    // M.1119222611.A.7A9
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.9.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.9.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1031, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.9.txt", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_9: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_9) {
    // M.1119222611.A.7A9
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.9.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.9.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1031, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.9.txt", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(25649, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_9_n_only) {
    // M.1119222611.A.7A9
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.9.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.9.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1002, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.9.txt_n_only", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_9: migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_9_n_only) {
    // M.1119222611.A.7A9
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.9.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.9.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1002, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.9.txt_n_only", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(25649, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_10) {
    // M.1.A.5CF
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.10.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.10.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2789, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.10.txt", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_10.migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_10) {
    // M.1.A.5CF
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.10.txt", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.10.txt", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2789, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.10.txt", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(44, n_comment_comment_reply);

    //free
}

TEST(migrate_file_to_pttdb, parse_legacy_file_main_info_10_n_only) {
    // M.1.A.5CF
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.10.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.10.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2718, legacy_file_info.main_content_len);

    char *buf = (char *)malloc(legacy_file_info.main_content_len + 1);
    bzero(buf, legacy_file_info.main_content_len + 1);

    int fd = open("data_test/original_msg.10.txt_n_only", O_RDONLY);
    read(fd, buf, legacy_file_info.main_content_len);
    fprintf(stderr, "test_migrate_file_to_pttdb.parse_legacy_file_main_info_10.migrate_file_to_buf: %s\n", buf);

    //free
    close(fd);
    free(buf);
}

TEST(migrate_file_to_pttdb, parse_legacy_file_n_comment_comment_reply_10_n_only) {
    // M.1.A.5CF
    LegacyFileInfo legacy_file_info = {};

    int file_size = 0;
    Err error = _parse_legacy_file_get_file_size("data_test/original_msg.10.txt_n_only", &file_size);
    EXPECT_EQ(S_OK, error);
    error = _parse_legacy_file_main_info("data_test/original_msg.10.txt_n_only", file_size, &legacy_file_info);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2718, legacy_file_info.main_content_len);

    int n_comment_comment_reply = 0;
    error = _parse_legacy_file_n_comment_comment_reply("data_test/original_msg.10.txt_n_only", legacy_file_info.main_content_len, file_size, &n_comment_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(44, n_comment_comment_reply);

    //free
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
    FD = open("log.test_migrate_file_to_pttdb.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
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
