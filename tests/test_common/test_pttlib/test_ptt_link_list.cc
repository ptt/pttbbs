#include "gtest/gtest.h"
#include "cmpttlib/ptt_link_list.h"

TEST(ptt_link_list, destroy_ptt_link_list) {
    PttLinkList *p = (PttLinkList *)malloc(sizeof(PttLinkList));
    bzero(p, sizeof(PttLinkList));
    p->val.d = 1;
    EXPECT_EQ(NULL, p->next);

    Err error = destroy_ptt_link_list(&p);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(NULL, p);
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
