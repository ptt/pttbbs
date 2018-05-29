#include "gtest/gtest.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "osdep.h"

#ifdef __cplusplus
}
#endif

TEST(strlcat, strlcat)
{
    char buf[1000] = {};

    sprintf(buf, "test1");

    size_t new_size = strlcat(buf, "test2", 11);

    EXPECT_EQ(10, new_size);
    EXPECT_STREQ("test1test2", buf);
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
    FD = open("log.osdep_test_strlcat.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
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
