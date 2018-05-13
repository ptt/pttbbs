#include "gtest/gtest.h"
#include "test.h"
#include "cmpttui/pttui_resource_dict.h"
#include "cmpttui/pttui_resource_dict_private.h"
#include "cmpttui/pttui_thread_lock.h"

TEST(pttui_resource_dict, pttui_resource_dict_get_main_from_db)
{
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

    FD = open("log.test_pttui_resource_dict.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
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

