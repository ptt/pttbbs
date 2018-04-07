#include "gtest/gtest.h"
#include "test.h"
#include "pttdb_util.h"

TEST(pttdb_util, get_line_from_buf) {
    int len_buf = 24;
    char buf[MAX_BUF_SIZE];
    char line[8192];
    int offset_buf = 0;
    int offset_line = 0;
    int bytes_in_new_line = 0;
    bzero(line, sizeof(line));

    fprintf(stderr, "test_pttdb_util.get_line_from_buf: start\n");
    strcpy(buf, "0123456789\r\nABCDEFGHIJ\r\n");

    Err error = get_line_from_buf(buf, offset_buf, len_buf, line, offset_line, 8192, &bytes_in_new_line);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(12, bytes_in_new_line);
    EXPECT_STREQ("0123456789\r\n", line);
}

TEST(pttdb_util, get_line_from_buf_with_offset_buf) {
    int len_buf = 24;
    char buf[MAX_BUF_SIZE];
    char line[8192];
    int offset_buf = 12;
    int offset_line = 0;
    int bytes_in_new_line = 0;
    bzero(line, sizeof(line));

    strcpy(buf, "0123456789\r\nABCDEFGHIJ\r\n");

    Err error = get_line_from_buf(buf, offset_buf, len_buf, line, offset_line, 8192, &bytes_in_new_line);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(12, bytes_in_new_line);
    EXPECT_STREQ("ABCDEFGHIJ\r\n", line);
}

TEST(pttdb_util, get_line_from_buf_with_line_offset) {
    int len_buf = 24;
    char buf[MAX_BUF_SIZE];
    char line[8192];
    int offset_buf = 0;
    int offset_line = 2;
    int bytes_in_new_line = 0;
    bzero(line, sizeof(line));

    strcpy(buf, "0123456789\r\nABCDEFGHIJ\r\n");
    line[0] = '!';
    line[1] = '@';

    Err error = get_line_from_buf(buf, offset_buf, len_buf, line, offset_line, 8192, &bytes_in_new_line);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(12, bytes_in_new_line);
    EXPECT_STREQ("!@0123456789\r\n", line);
}

TEST(pttdb_util, get_line_from_buf_not_end) {
    int len_buf = 10;
    char buf[MAX_BUF_SIZE];
    char line[8192];
    int offset_buf = 0;
    int offset_line = 0;
    int bytes_in_new_line = 0;
    bzero(line, sizeof(line));

    strcpy(buf, "0123456789");

    Err error = get_line_from_buf(buf, offset_buf, len_buf, line, offset_line, 8192, &bytes_in_new_line);
    EXPECT_EQ(S_ERR, error);
    EXPECT_EQ(10, bytes_in_new_line);
    EXPECT_STREQ("0123456789", line);
}

TEST(pttdb_util, get_line_from_buf_offset_buf_not_end) {
    int len_buf = 13;
    char buf[MAX_BUF_SIZE];
    char line[8192];
    int offset_buf = 3;
    int offset_line = 0;
    int bytes_in_new_line = 0;
    bzero(line, sizeof(line));

    strcpy(buf, "A\r\n0123456789");

    Err error = get_line_from_buf(buf, offset_buf, len_buf, line, offset_line, 8192, &bytes_in_new_line);
    EXPECT_EQ(S_ERR, error);
    EXPECT_EQ(10, bytes_in_new_line);
    EXPECT_STREQ("0123456789", line);
}

TEST(pttdb_util, get_line_from_buf_r_only) {
    int len_buf = 13;
    char buf[MAX_BUF_SIZE];
    char line[8192];
    int offset_buf = 0;
    int offset_line = 0;
    int bytes_in_new_line = 0;
    bzero(line, sizeof(line));

    strcpy(buf, "A\r0123456789");

    Err error = get_line_from_buf(buf, offset_buf, len_buf, line, offset_line, 8192, &bytes_in_new_line);
    EXPECT_EQ(S_ERR, error);
    EXPECT_EQ(13, bytes_in_new_line);
    EXPECT_STREQ("A\r0123456789", line);
}

TEST(pttdb_util, get_line_from_buf_n_only) {
    int len_buf = 13;
    char buf[MAX_BUF_SIZE];
    char line[8192];
    int offset_buf = 0;
    int offset_line = 0;
    int bytes_in_new_line = 0;
    bzero(line, sizeof(line));

    strcpy(buf, "A\n0123456789");

    Err error = get_line_from_buf(buf, offset_buf, len_buf, line, offset_line, 8192, &bytes_in_new_line);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2, bytes_in_new_line);
    EXPECT_STREQ("A\n", line);
}

TEST(pttdb_util, get_line_from_buf_line_with_max_buf_size) {
    int len_buf = 22;
    char buf[MAX_BUF_SIZE] = {};
    char line[8192] = {};
    int offset_buf = 0;
    int offset_line = 8192 - 10;
    int bytes_in_new_line = 0;

    strcpy(buf, "01234567890123456789\r\n");

    for(int i = 0; i < offset_line; i++) line[i] = 'A';

    Err error = get_line_from_buf(buf, offset_buf, len_buf, line, offset_line, 8192, &bytes_in_new_line);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, bytes_in_new_line);
    EXPECT_EQ(0, strncmp("0123456789", line + offset_line, 10));
}

TEST(pttdb_util, get_line_from_buf_partial_line_break) {
    int len_buf = 13;
    char buf[MAX_BUF_SIZE];
    char line[8192];
    int offset_buf = 0;
    int offset_line = 2;
    int bytes_in_new_line = 0;
    bzero(line, sizeof(line));

    strcpy(buf, "\n0123456789\r\n");
    strcpy(line, "!\r");

    Err error = get_line_from_buf(buf, offset_buf, len_buf, line, offset_line, 8192, &bytes_in_new_line);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1, bytes_in_new_line);
    EXPECT_STREQ("!\r\n", line);
}

TEST(pttdb_util, get_line_from_buf_end_of_buf) {
    int len_buf = 12;
    char buf[MAX_BUF_SIZE];
    char line[8192];
    int offset_buf = 12;
    int offset_line = 0;
    int bytes_in_new_line = 0;
    bzero(line, sizeof(line));

    strcpy(buf, "0123456789\r\n");

    Err error = get_line_from_buf(buf, offset_buf, len_buf, line, offset_line, 8192, &bytes_in_new_line);
    EXPECT_EQ(S_ERR, error);
    EXPECT_EQ(0, bytes_in_new_line);
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
    FD = open("log.test_pttdb_util.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
    dup2(FD, 2);
}

void MyEnvironment::TearDown() {
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
