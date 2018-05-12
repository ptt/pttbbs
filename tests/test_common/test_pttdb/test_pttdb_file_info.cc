#include "gtest/gtest.h"
#include "test.h"
#include "cmpttdb/pttdb_main.h"
#include "cmpttdb/pttdb_comment.h"
#include "cmpttdb/pttdb_comment_reply.h"
#include "cmpttdb/pttdb_file_info.h"
#include "cmmigrate_pttdb.h"

TEST(pttdb_file_info, construct_file_info) {
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    error = construct_file_info(main_id, &file_info);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(main_id, file_info.main_id, UUIDLEN));
    EXPECT_STREQ((char *)"poster0", file_info.main_poster);
    EXPECT_EQ(1511576360000L, file_info.main_create_milli_timestamp);
    EXPECT_EQ(1511576360000L, file_info.main_update_milli_timestamp);
    EXPECT_EQ(41, file_info.n_main_line);
    EXPECT_EQ(1, file_info.n_main_block);
    EXPECT_EQ(103, file_info.n_comment);
    EXPECT_EQ(100, file_info.comments[89].n_comment_reply_total_line);
    EXPECT_EQ(2, file_info.comments[89].n_comment_reply_block);
    EXPECT_EQ(80, file_info.comments[89].comment_reply_blocks[0].n_line);
    EXPECT_EQ(20, file_info.comments[89].comment_reply_blocks[1].n_line);

    error = destroy_file_info(&file_info);
    EXPECT_EQ(S_OK, error);
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
    Err err = S_OK;

    FD = open("log.test_pttdb_file_info.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
    dup2(FD, 2);

    const char *db_name[] = {
        "test_post",
        "test",
    };

    err = init_mongo_global();
    if (err != S_OK) {
        fprintf(stderr, "[ERROR] UNABLE TO init mongo global\n");
        return;
    }
    err = init_mongo_collections(db_name);
    if (err != S_OK) {
        fprintf(stderr, "[ERROR] UNABLE TO init mongo collections\n");
        return;
    }

    FILE *f = fopen("data_test/test1.txt", "w");
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < 1000; i++) {
            fprintf(f, "%c", 64 + (i % 26));
        }
        fprintf(f, "\r\n");
    }
    fclose(f);    
}

void MyEnvironment::TearDown() {
    free_mongo_collections();
    free_mongo_global();

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

