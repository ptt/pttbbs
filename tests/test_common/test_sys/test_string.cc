#include "gtest/gtest.h"

extern "C" {
    #include "cmsys.h"
}

TEST(sys_string, str_lower) {
    char t[256] = {};
    char s[] = " !\"#$%%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    str_lower(t, s);
    EXPECT_STREQ(" !\"#$%%&'()*+,-./0123456789:;<=>?@abcdefghijklmnopqrstuvwxyz[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~", t);
}

TEST(sys_string, str_starts_with) {
    EXPECT_EQ(1, str_starts_with("abcd", "abc"));
    EXPECT_EQ(0, str_starts_with("abcd", "acd"));
    EXPECT_EQ(0, str_starts_with("abcd", "def"));
}

/**********
 * MAIN
 */
class MyEnvironment: public ::testing::Environment {
public:
    void SetUp();
    void TearDown();
};

void MyEnvironment::SetUp() {
}

void MyEnvironment::TearDown() {
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new MyEnvironment);

    return RUN_ALL_TESTS();
}
