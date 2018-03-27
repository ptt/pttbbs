#include "gtest/gtest.h"
#include "bbs.h"
#include "ptterr.h"
#include "pttdb.h"
#include "pttdb_internal.h"
#include "util_db_internal.h"

TEST(pttdb_comment_reply, create_comment_reply) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);

    UUID main_id;
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char content[] = "temp_content\r\n";
    int len = strlen(content);
    int n_line = 1;

    UUID comment_id;
    UUID comment_reply_id;
    UUID tmp_comment_reply_id;

    gen_uuid(main_id);
    gen_uuid(comment_reply_id);

    Err error_code = create_comment_reply(main_id, comment_id, poster, ip, len, content, comment_reply_id);
    EXPECT_EQ(S_OK, error_code);

    CommentReply comment_reply = {};
    init_comment_reply_buf(&comment_reply);

    memcpy(tmp_comment_reply_id, comment_reply_id, UUIDLEN);

    error_code = read_comment_reply(comment_reply_id, &comment_reply);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(0, strncmp((char *)tmp_comment_reply_id, (char *)comment_reply.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)comment_id, (char *)comment_reply.comment_id, UUIDLEN));

    EXPECT_EQ(0, strncmp((char *)main_id, (char *)comment_reply.main_id, UUIDLEN));
    EXPECT_EQ(LIVE_STATUS_ALIVE, comment_reply.status);

    EXPECT_STREQ(poster, comment_reply.status_updater);
    EXPECT_STREQ(ip, comment_reply.status_update_ip);

    EXPECT_STREQ(poster, comment_reply.poster);
    EXPECT_STREQ(ip, comment_reply.ip);

    EXPECT_STREQ(poster, comment_reply.updater);
    EXPECT_STREQ(ip, comment_reply.update_ip);

    EXPECT_EQ(comment_reply.create_milli_timestamp, comment_reply.update_milli_timestamp);

    EXPECT_EQ(len, comment_reply.len);
    EXPECT_STREQ(content, comment_reply.buf);
    EXPECT_EQ(n_line, comment_reply.n_line);

    destroy_comment_reply(&comment_reply);
}

TEST(pttdb_comment_reply, delete_comment_reply) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);

    UUID main_id;
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char content[] = "temp_content";
    int len = strlen(content);

    UUID comment_reply_id;
    UUID comment_id;    

    gen_uuid(main_id);
    gen_uuid(comment_id);

    Err error = create_comment_reply(main_id, comment_id, poster, ip, len, content, comment_reply_id);
    EXPECT_EQ(S_OK, error);

    char del_updater[IDLEN + 1] = "del_updater";
    char status_update_ip[IPV4LEN + 1] = "10.1.1.4";
    error = delete_comment_reply(comment_reply_id, del_updater, status_update_ip);
    EXPECT_EQ(S_OK, error);

    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "status", BCON_BOOL(true),
        "status_updater", BCON_BOOL(true),
        "status_update_ip", BCON_BOOL(true)
        );

    bson_t *query = BCON_NEW(
        "the_id", BCON_BINARY(comment_reply_id, UUIDLEN)
    );
    bson_t *result = NULL;

    error = db_find_one(MONGO_COMMENT_REPLY, query, fields, &result);
    EXPECT_EQ(S_OK, error);
    int result_status;
    char result_status_updater[MAX_BUF_SIZE];
    char result_status_update_ip[MAX_BUF_SIZE];
    bson_get_value_int32(result, (char *)"status", &result_status);
    bson_get_value_bin(result, (char *)"status_updater", MAX_BUF_SIZE, result_status_updater, &len);
    bson_get_value_bin(result, (char *)"status_update_ip", MAX_BUF_SIZE, result_status_update_ip, &len);

    EXPECT_EQ(LIVE_STATUS_DELETED, result_status);
    EXPECT_STREQ(del_updater, result_status_updater);
    EXPECT_STREQ(status_update_ip, result_status_update_ip);

    bson_safe_destroy(&query);
    bson_safe_destroy(&fields);
    bson_safe_destroy(&result);
}

TEST(pttdb_comment_reply, get_comment_reply_info_by_main) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    UUID main_id = {};
    UUID comment_reply_id = {};
    UUID comment_reply_id2 = {};
    UUID comment_id = {};
    UUID comment_id2 = {};

    gen_uuid(main_id);
    gen_uuid(comment_id);
    gen_uuid(comment_id2);

    Err error = S_OK;
    error = create_comment_reply(main_id, comment_id, (char *)"poster1", (char *)"10.3.1.4", 24, (char *)"test1test1\r\ntest3test3\r\n", comment_reply_id);
    EXPECT_EQ(S_OK, error);
    error = create_comment_reply(main_id, comment_id2, (char *)"poster1", (char *)"10.3.1.4", 12, (char *)"test2test2\r\n", comment_reply_id2);
    EXPECT_EQ(S_OK, error);

    int n_total_comments = 0;
    int total_len = 0;
    int n_line = 0;
    error = get_comment_reply_info_by_main(main_id, &n_total_comments, &n_line, &total_len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2, n_total_comments);
    EXPECT_EQ(3, n_line);
    EXPECT_EQ(36, total_len);
}

TEST(pttdb_comment_reply, serialize_comment_reply_bson) {
    CommentReply comment_reply = {};
    CommentReply comment_reply2 = {};

    init_comment_reply_buf(&comment_reply);
    init_comment_reply_buf(&comment_reply2);

    comment_reply.version = 2;
    gen_uuid(comment_reply.the_id);
    memcpy(comment_reply.comment_id, comment_reply.the_id, UUIDLEN);
    gen_uuid(comment_reply.main_id);
    comment_reply.status = LIVE_STATUS_ALIVE;

    strcpy(comment_reply.status_updater, "updater1");
    strcpy(comment_reply.status_update_ip, "10.3.1.4");

    strcpy(comment_reply.poster, "poster1");
    strcpy(comment_reply.ip, "10.3.1.5");
    comment_reply.create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST
    strcpy(comment_reply.updater, "updater2");
    strcpy(comment_reply.update_ip, "10.3.1.6");
    comment_reply.update_milli_timestamp = 1514764801000; //2018-01-01 08:00:01 CST

    strcpy(comment_reply.buf, "test_buf\r\n");
    comment_reply.len = strlen(comment_reply.buf);
    comment_reply.n_line = 1;

    bson_t *comment_reply_bson = NULL;

    Err error = _serialize_comment_reply_bson(&comment_reply, &comment_reply_bson);
    EXPECT_EQ(S_OK, error);

    char *str = bson_as_canonical_extended_json(comment_reply_bson, NULL);
    fprintf(stderr, "test_pttdb_comment_reply.serialize_comment_reply_bson: comment_reply_bson: %s\n", str);
    bson_free(str);

    error = _deserialize_comment_reply_bson(comment_reply_bson, &comment_reply2);

    bson_safe_destroy(&comment_reply_bson);

    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(comment_reply.version, comment_reply2.version);
    EXPECT_EQ(0, strncmp((char *)comment_reply.the_id, (char *)comment_reply2.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)comment_reply.comment_id, (char *)comment_reply2.comment_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)comment_reply.main_id, (char *)comment_reply2.main_id, UUIDLEN));
    EXPECT_EQ(comment_reply.status, comment_reply2.status);
    EXPECT_STREQ(comment_reply.status_updater, comment_reply2.status_updater);
    EXPECT_STREQ(comment_reply.status_update_ip, comment_reply2.status_update_ip);
    EXPECT_STREQ(comment_reply.poster, comment_reply2.poster);
    EXPECT_STREQ(comment_reply.ip, comment_reply2.ip);
    EXPECT_EQ(comment_reply.create_milli_timestamp, comment_reply2.create_milli_timestamp);
    EXPECT_STREQ(comment_reply.updater, comment_reply2.updater);
    EXPECT_STREQ(comment_reply.update_ip, comment_reply2.update_ip);
    EXPECT_EQ(comment_reply.update_milli_timestamp, comment_reply2.update_milli_timestamp);
    EXPECT_EQ(comment_reply.len, comment_reply2.len);
    EXPECT_EQ(comment_reply.n_line, comment_reply2.n_line);
    EXPECT_STREQ(comment_reply.buf, comment_reply2.buf);

    destroy_comment_reply(&comment_reply);
    destroy_comment_reply(&comment_reply2);
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

    FD = open("log.test_pttdb_comment_reply.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
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
