#include "gtest/gtest.h"
#include "bbs.h"

TEST(bbs, is_file_owner) {
    // init
    fileheader_t fhdr = {};
    userec_t usr = {};

    // prepare data
    sprintf(fhdr.owner, "owner1");
    sprintf(usr.userid, "owner1");
    sprintf(fhdr.filename, "M.1234567890.A.ABC");
    usr.firstlogin = 1234567880;

    int ret = is_file_owner(&fhdr, &usr);

    EXPECT_EQ(1, ret);

    // free
}

TEST(bbs, is_file_owner_ne_owner) {
    // init
    fileheader_t fhdr = {};
    userec_t usr = {};

    // prepare data
    sprintf(fhdr.owner, "owner1");
    sprintf(usr.userid, "owner2");

    int ret = is_file_owner(&fhdr, &usr);

    EXPECT_EQ(0, ret);

    // free
}

TEST(bbs, is_file_owner_unknown) {
    // init
    fileheader_t fhdr = {};
    userec_t usr = {};

    // prepare data
    sprintf(fhdr.owner, "owner1");
    sprintf(usr.userid, "owner1");

    int ret = is_file_owner(&fhdr, &usr);

    EXPECT_EQ(0, ret);

    // free
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
    FD = open("log.mbbsd_test_bbs.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
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