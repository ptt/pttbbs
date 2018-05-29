#include "gtest/gtest.h"
#include "test.h"
#include "cmpttui/pttui_thread.h"
#include "cmpttui/pttui_thread_private.h"

TEST(pttui_thread, PttUIThreadSetExpectedState) {
    Err error = PttUIThreadSetExpectedState(PTTUI_THREAD_STATE_END);
    EXPECT_EQ(S_OK, error);

    enum PttUIThreadState thread_state = PTTUI_THREAD_STATE_START;
    error = PttUIThreadGetExpectedState(&thread_state);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(PTTUI_THREAD_STATE_END, thread_state);
}

TEST(pttui_thread, PttUIThreadSetBufferState) {
    Err error = PttUIThreadSetBufferState(PTTUI_THREAD_STATE_END);
    EXPECT_EQ(S_OK, error);

    enum PttUIThreadState thread_state = PTTUI_THREAD_STATE_START;
    error = PttUIThreadGetBufferState(&thread_state);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(PTTUI_THREAD_STATE_END, thread_state);
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
    FD = open("log.cmpttui_thread_test_pttui_thread.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
    dup2(FD, 2);

    InitPttUIThread();
}

void MyEnvironment::TearDown() {
    DestroyPttUIThread();

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
