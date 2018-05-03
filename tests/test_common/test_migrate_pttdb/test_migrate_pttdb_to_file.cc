#include "gtest/gtest.h"
#include "test.h"
#include "cmpttdb.h"
#include "cmmigrate_pttdb/migrate_pttdb_to_file.h"

TEST(migrate_pttdb_to_file, migrate_pttdb_to_file) {
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);

    int fd = open("data_test/test1.txt", O_RDONLY);

    aidu_t aid = 12345;
    char board[IDLEN + 1] = {};
    char title[TTLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char origin[MAX_ORIGIN_LEN + 1] = {};
    char web_link[MAX_WEB_LINK_LEN + 1] = {};
    int len = 10020;
    UUID main_id;
    UUID content_id;

    strcpy(board, "test_board");
    strcpy(title, "test_title");
    strcpy(poster, "test_poster");
    strcpy(ip, "test_ip");
    strcpy(origin, "ptt.cc");
    strcpy(web_link, "http://www.ptt.cc/bbs/longboardid/M.1234567890.ABCD.html");

    // create-main-from-fd
    Err error = create_main_from_fd(aid, board, title, poster, ip, origin, web_link, len, fd, main_id, content_id, 0);
    EXPECT_EQ(S_OK, error);

    close(fd);

    char *disp_uuid = NULL;
    disp_uuid = display_uuid(main_id);
    fprintf(stderr, "test_pttdb_migrate_pttdb_to_file.migrate_pttdb_to_file: after create_main_from_fd: main_id: %s\n", disp_uuid);
    safe_free((void **)&disp_uuid);

    Comment comment = {};
    init_comment_buf(&comment);
    // comment-comment-reply
    memcpy(comment.main_id, main_id, sizeof(UUID));
    comment.status = LIVE_STATUS_ALIVE;
    strcpy(comment.status_updater, "poster000");
    strcpy(comment.status_update_ip, "10.1.1.4");

    time64_t create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST

    comment.comment_type = COMMENT_TYPE_GOOD;
    comment.karma = KARMA_BY_COMMENT_TYPE[COMMENT_TYPE_GOOD];

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    char replier[IDLEN + 1] = {};
    char reply_buf[MAX_BUF_SIZE] = {};

    UUID comment_id = {};
    UUID comment_reply_id = {};

    for (int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        sprintf(comment.buf, "testtest0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789%03d\r\n", i);
        comment.len = strlen(comment.buf);


        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        sprintf(replier, "reply%03d", i);
        sprintf(reply_buf, "replyreply%03d\r\n", i);
        error = create_comment_reply(main_id, comment_id, replier, (char *)"10.1.1.5", strlen(reply_buf), reply_buf, comment_reply_id, 0);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for (int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        sprintf(comment.buf, "testtest0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789%03d\r\n", i);
        comment.len = strlen(comment.buf);

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for (int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        sprintf(comment.buf, "testtest0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789%03d\r\n", i);
        comment.len = strlen(comment.buf);

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // migrate-db-to-file
    error = migrate_pttdb_to_file(main_id, "data_test/test_migrate_pttdb_to_file_result.txt");
    EXPECT_EQ(S_OK, error);

    // free

    destroy_comment(&comment);
}

TEST(migrate_pttdb_to_file, migrate_pttdb_to_file_n_only) {
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);

    int fd = open("data_test/test1.txt", O_RDONLY);

    aidu_t aid = 12345;
    char board[IDLEN + 1] = {};
    char title[TTLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char origin[MAX_ORIGIN_LEN + 1] = {};
    char web_link[MAX_WEB_LINK_LEN + 1] = {};
    int len = 10020;
    UUID main_id;
    UUID content_id;

    strcpy(board, "test_board");
    strcpy(title, "test_title");
    strcpy(poster, "test_poster");
    strcpy(ip, "test_ip");
    strcpy(origin, "ptt.cc");
    strcpy(web_link, "http://www.ptt.cc/bbs/longboardid/M.1234567890.ABCD.html");

    // create-main-from-fd
    Err error = create_main_from_fd(aid, board, title, poster, ip, origin, web_link, len, fd, main_id, content_id, 0);
    EXPECT_EQ(S_OK, error);

    close(fd);

    char *disp_uuid = NULL;
    disp_uuid = display_uuid(main_id);
    fprintf(stderr, "test_pttdb_migrate_pttdb_to_file.migrate_pttdb_to_file: after create_main_from_fd: main_id: %s\n", disp_uuid);
    safe_free((void **)&disp_uuid);

    Comment comment = {};
    init_comment_buf(&comment);
    // comment-comment-reply
    memcpy(comment.main_id, main_id, sizeof(UUID));
    comment.status = LIVE_STATUS_ALIVE;
    strcpy(comment.status_updater, "poster000");
    strcpy(comment.status_update_ip, "10.1.1.4");

    time64_t create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST

    comment.comment_type = COMMENT_TYPE_GOOD;
    comment.karma = KARMA_BY_COMMENT_TYPE[COMMENT_TYPE_GOOD];

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    char replier[IDLEN + 1] = {};
    char reply_buf[MAX_BUF_SIZE] = {};

    UUID comment_id = {};
    UUID comment_reply_id = {};

    for (int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        sprintf(comment.buf, "testtest0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789%03d\n", i);
        comment.len = strlen(comment.buf);


        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        sprintf(replier, "reply%03d", i);
        sprintf(reply_buf, "replyreply%03d\n", i);
        error = create_comment_reply(main_id, comment_id, replier, (char *)"10.1.1.5", strlen(reply_buf), reply_buf, comment_reply_id, 0);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for (int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        sprintf(comment.buf, "testtest0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789%03d\n", i);
        comment.len = strlen(comment.buf);

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for (int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        sprintf(comment.buf, "testtest0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789%03d\n", i);
        comment.len = strlen(comment.buf);

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // migrate-db-to-file
    error = migrate_pttdb_to_file(main_id, "data_test/test_migrate_pttdb_to_file_result_n_only.txt");
    EXPECT_EQ(S_OK, error);

    // free

    destroy_comment(&comment);
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
    FD = open("log.test_migrate_pttdb_to_file.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
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
