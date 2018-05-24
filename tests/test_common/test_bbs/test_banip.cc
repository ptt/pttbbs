#include "gtest/gtest.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "cmbbs.h"

#ifdef __cplusplus
}
#endif

TEST(banip, load_banip_list)
{
    BanIpList *ban_ip_list = load_banip_list("data_test/not_exists.conf", stderr);
    EXPECT_EQ((void *)NULL, ban_ip_list);

    ban_ip_list = load_banip_list("data_test/banip.conf", stderr);
    EXPECT_NE((void *)NULL, ban_ip_list);

    // free
    free_banip_list(ban_ip_list);
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
    FD = open("log.cmbbs_test_banip.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
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
