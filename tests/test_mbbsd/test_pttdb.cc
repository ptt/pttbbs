#include "gtest/gtest.h"
#include "bbs.h"
#include "ptterr.h"
#include "pttdb.h"
#include "pttdb_internal.h"
#include "util_db_internal.h"

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
    Err err = S_OK;

    FD = open("log.test_pttdb.err", O_WRONLY|O_CREAT|O_TRUNC, 0660);
    dup2(FD, 2);

    const char *db_name[] = {
        "test_post",
        "test",
    };

    err = init_mongo_global();
    if(err != S_OK) {
        fprintf(stderr, "[ERROR] UNABLE TO init mongo global\n");
        return;
    }
    err = init_mongo_collections(db_name);
    if(err != S_OK) {
        fprintf(stderr, "[ERROR] UNABLE TO init mongo collections\n");
        return;
    }
}

void MyEnvironment::TearDown() {
    free_mongo_collections();
    free_mongo_global();

    if(FD) {
        close(FD);
        FD = 0;
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new MyEnvironment);

    return RUN_ALL_TESTS();
}
