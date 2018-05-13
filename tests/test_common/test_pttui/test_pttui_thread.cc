#include "gtest/gtest.h"
#include "test.h"
#include "cmpttui/pttui_thread.h"
#include "cmpttui/pttui_thread_private.h"

TEST(pttui_thread, pttui_thread_set_expected_state) {
    Err error = pttui_thread_set_expected_state(PTTUI_THREAD_STATE_EDIT);
    EXPECT_EQ(S_OK, error);

    enum PttUIThreadState thread_state = PTTUI_THREAD_STATE_START;
    error = pttui_thread_get_expected_state(&thread_state);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(PTTUI_THREAD_STATE_EDIT, thread_state);
}

TEST(pttui_thread, pttui_thread_set_buffer_state) {
    Err error = pttui_thread_set_buffer_state(PTTUI_THREAD_STATE_EDIT);
    EXPECT_EQ(S_OK, error);

    enum PttUIThreadState thread_state = PTTUI_THREAD_STATE_START;
    error = pttui_thread_get_buffer_state(&thread_state);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(PTTUI_THREAD_STATE_EDIT, thread_state);
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
    pttui_thread_lock_init();

    FD = open("log.test_pttui_thread.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
    dup2(FD, 2);

}

void MyEnvironment::TearDown() {
    pttui_thread_lock_destroy();

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

