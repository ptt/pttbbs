#include "gtest/gtest.h"
#include "cmpttlib/ptt_queue.h"

TEST(ptt_queue, ptt_enqueue_p) {
    int a[] = {0, 1, 2, 3, 4, 5};

    PttQueue q = {};

    Err error = S_OK;
    for(int i = 0; i < 6; i++) {
        error = ptt_enqueue_p(&a[i], &q);
        EXPECT_EQ(S_OK, error);
    }

    PttLinkList *current = NULL;
    int i = 0;
    for(i = 0, current = q.head; i < 6; i++, current = current->next) {
        EXPECT_EQ(i, *((int *)current->val.p));
    }

    error = destroy_ptt_queue(&q);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(NULL, q.head);
    EXPECT_EQ(NULL, q.tail);
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
