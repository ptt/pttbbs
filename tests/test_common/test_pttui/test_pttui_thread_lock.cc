#include "gtest/gtest.h"
#include "test.h"
#include "cmpttui/pttui_thread_lock.h"
#include "cmpttui/pttui_thread_lock_private.h"

TEST(pttui_thread_lock, PttUIThreadLockWrlock)
{
    Err error = PttUIThreadLockWrlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    pthread_rwlock_t *p_lock = NULL;
    error = PttUIThreadLockGetLock(LOCK_PTTUI_THREAD_EXPECTED_STATE, &p_lock);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockWrlock: after lock: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockUnlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockWrlock: after unlock: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);
}

TEST(pttui_thread_lock, PttUIThreadLockWrlock2) {
    Err error = PttUIThreadLockWrlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    pthread_rwlock_t *p_lock = NULL;
    error = PttUIThreadLockGetLock(LOCK_PTTUI_THREAD_EXPECTED_STATE, &p_lock);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockWrlock2: after lock: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockWrlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_ERR_BUSY, error);

    error = PttUIThreadLockGetLock(LOCK_PTTUI_THREAD_EXPECTED_STATE, &p_lock);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockWrlock2: after lock2: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockUnlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockWrlock2: after unlock: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockWrlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    error = PttUIThreadLockGetLock(LOCK_PTTUI_THREAD_EXPECTED_STATE, &p_lock);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockWrlock2: after lock3: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockUnlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockWrlock2: after unlock: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);
}

TEST(pttui_thread_lock, PttUIThreadLockRdlock)
{
    Err error = PttUIThreadLockRdlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    pthread_rwlock_t *p_lock = NULL;
    error = PttUIThreadLockGetLock(LOCK_PTTUI_THREAD_EXPECTED_STATE, &p_lock);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockRdlock: after lock: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockRdlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    error = PttUIThreadLockGetLock(LOCK_PTTUI_THREAD_EXPECTED_STATE, &p_lock);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockRdlock: after lock2: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockUnlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockRdlock: after unlock: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockUnlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockRdlock: after unlock: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);
}

TEST(pttui_thread_lock, PttUIThreadLockRdlock2)
{
    Err error = PttUIThreadLockRdlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    pthread_rwlock_t *p_lock = NULL;
    error = PttUIThreadLockGetLock(LOCK_PTTUI_THREAD_EXPECTED_STATE, &p_lock);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockRdlock2: after lock: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockRdlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    error = PttUIThreadLockGetLock(LOCK_PTTUI_THREAD_EXPECTED_STATE, &p_lock);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockRdlock2: after lock2: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockWrlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_ERR_BUSY, error);

    error = PttUIThreadLockGetLock(LOCK_PTTUI_THREAD_EXPECTED_STATE, &p_lock);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockRdlock2: after lock2: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockUnlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockRdlock2: after unlock: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

    error = PttUIThreadLockUnlock(LOCK_PTTUI_THREAD_EXPECTED_STATE);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttui_thread_lock.PttUIThreadLockRdlock2: after unlock: p_lock: (lock: %d nr_readers: %u readers_wakeup: %u writer_wakeup: %u nr_readers_queued: %u nr_writers_queued: %u writer: %d shared: %d rwelision: %d flags: %u\n", p_lock->__data.__lock, p_lock->__data.__nr_readers, p_lock->__data.__readers_wakeup, p_lock->__data.__writer_wakeup, p_lock->__data.__nr_readers_queued, p_lock->__data.__nr_writers_queued, p_lock->__data.__writer, p_lock->__data.__shared, p_lock->__data.__rwelision, p_lock->__data.__flags);

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
    FD = open("log.cmpttui_thread_lock_test_pttui_thread_lock.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
    dup2(FD, 2);

    InitPttUIThreadLock();
}

void MyEnvironment::TearDown() {
    DestroyPttUIThreadLock();

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
