#include "gtest/gtest.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "cmsys.h"

#ifdef __cplusplus
}
#endif

TEST(utf8, ucs2utf)
{
    uint16_t ucs2 = 65;
    unsigned char utf8[4] = {};

    int n_size = ucs2utf(ucs2, utf8);

    EXPECT_EQ(1, n_size);
    EXPECT_STREQ((char *)"A", (char *)utf8);
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
    FD = open("log.cmsys_test_utf8.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
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
