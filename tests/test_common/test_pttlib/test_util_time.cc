#define _XOPEN_SOURCE
#include "gtest/gtest.h"
#include "test.h"
#include "cmpttlib/util_time.h"
#include "cmpttlib/util_time_private.h"

// 2018-01-01
time64_t _START_MILLI_TIMESTAMP = 1514764800000;

// 2019-01-01
time64_t _END_MILLI_TIMESTAMP = 1546300800000;

TEST(util_time, GetMilliTimestamp)
{
    time64_t milli_timestamp = 0;
    Err error_code = GetMilliTimestamp(&milli_timestamp);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_GT(milli_timestamp, _START_MILLI_TIMESTAMP);
    EXPECT_LT(milli_timestamp, _END_MILLI_TIMESTAMP);
}

TEST(util_time, MilliTimestampToYear)
{
    time64_t milli_timestamp = 1514764800000;
    int year = 0;
    Err error_code = MilliTimestampToYear(milli_timestamp, &year);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(2018, year);
}

TEST(util_time, MilliTimestampToTimestamp_ne)
{
    time64_t milli_timestamp = 1514764800000;
    time64_t the_timestamp = MilliTimestampToTimestamp_ne(milli_timestamp);
    EXPECT_EQ(1514764800, the_timestamp);
}

TEST(util_time, DatetimeToTimestamp)
{
    time64_t the_timestamp = 0;
    Err error_code = DatetimeToTimestamp(2018, 1, 1, 0, 0, 0, 0, &the_timestamp);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(1514764800, the_timestamp);
}

TEST(util_time, MilliTimestampToCdate_ne)
{
    time64_t milli_timestamp = 1514764800000;
    const char *str = MilliTimestampToCdate_ne(milli_timestamp);
    EXPECT_STREQ("01/01/2018 08:00:00 Mon", str);
}

TEST(util_time, MilliTimestampToCdateLite_ne)
{
    time64_t milli_timestamp = 1514764800000;
    const char *str = MilliTimestampToCdateLite_ne(milli_timestamp);
    EXPECT_STREQ("01/01/2018 08:00:00", str);
}

TEST(util_time, MilliTimestampToCdateDate_ne)
{
    time64_t milli_timestamp = 1514764800000;
    const char *str = MilliTimestampToCdateDate_ne(milli_timestamp);
    EXPECT_STREQ("01/01/2018", str);
}

TEST(util_time, MilliTimestampToCdateMd_ne)
{
    time64_t milli_timestamp = 1514764800000;
    const char *str = MilliTimestampToCdateMd_ne(milli_timestamp);
    EXPECT_STREQ("01/01", str);
}

TEST(util_time, MilliTimestampToCdateMdHM_ne)
{
    time64_t milli_timestamp = 1514764800000;
    const char *str = MilliTimestampToCdateMdHM_ne(milli_timestamp);
    EXPECT_STREQ("01/01 08:00", str);
}

TEST(util_time, MilliTimestampToCdateMdHMS_ne)
{
    time64_t milli_timestamp = 1514764800000;
    const char *str = MilliTimestampToCdateMdHMS_ne(milli_timestamp);
    EXPECT_STREQ("01/01 08:00:00", str);
}

TEST(util_time, AddTimespecWithNanosecs)
{
    struct timespec t = {};
    Err error_code = AddTimespecWithNanosecs(&t, 999999999);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(0, t.tv_sec);
    EXPECT_EQ(999999999, t.tv_nsec);

    error_code = AddTimespecWithNanosecs(&t, 999999999);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(1, t.tv_sec);
    EXPECT_EQ(999999998, t.tv_nsec);
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
    FD = open("log.cmutil_time_test_util_time.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
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
