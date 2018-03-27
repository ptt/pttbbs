#include "gtest/gtest.h"
#include "bbs.h"
#include "ptterr.h"
#include "pttdb.h"
#include "pttdb_internal.h"
#include "util_db_internal.h"

TEST(pttdb_comment, create_comment) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    UUID main_id;
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char content[] = "temp_content";
    int len = strlen(content);
    enum CommentType comment_type = COMMENT_TYPE_GOOD;

    UUID comment_id;
    UUID tmp_comment_id;

    gen_uuid(main_id);
    Err error_code = create_comment(main_id, poster, ip, len, content, comment_type, comment_id);
    EXPECT_EQ(S_OK, error_code);

    Comment comment = {};
    init_comment_buf(&comment);

    memcpy(tmp_comment_id, comment_id, UUIDLEN);

    error_code = read_comment(comment_id, &comment);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(0, strncmp((char *)tmp_comment_id, (char *)comment.the_id, UUIDLEN));

    EXPECT_EQ(0, strncmp((char *)main_id, (char *)comment.main_id, UUIDLEN));
    EXPECT_EQ(LIVE_STATUS_ALIVE, comment.status);

    EXPECT_STREQ(poster, comment.status_updater);
    EXPECT_STREQ(ip, comment.status_update_ip);

    EXPECT_EQ(comment_type, comment.comment_type);
    EXPECT_EQ(KARMA_GOOD, comment.karma);

    EXPECT_STREQ(poster, comment.poster);
    EXPECT_STREQ(ip, comment.ip);

    EXPECT_STREQ(poster, comment.updater);
    EXPECT_STREQ(ip, comment.update_ip);

    EXPECT_EQ(comment.create_milli_timestamp, comment.update_milli_timestamp);

    EXPECT_EQ(len, comment.len);
    EXPECT_STREQ(content, comment.buf);

    destroy_comment(&comment);
}

TEST(pttdb_comment, delete_comment) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    UUID main_id;
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char content[] = "temp_content";
    int len = strlen(content);
    enum CommentType comment_type = COMMENT_TYPE_GOOD;

    UUID comment_id;

    gen_uuid(main_id);
    Err error = create_comment(main_id, poster, ip, len, content, comment_type, comment_id);
    EXPECT_EQ(S_OK, error);

    char del_updater[IDLEN + 1] = "del_updater";
    char status_update_ip[IPV4LEN + 1] = "10.1.1.4";
    error = delete_comment(comment_id, del_updater, status_update_ip);
    EXPECT_EQ(S_OK, error);

    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "status", BCON_BOOL(true),
        "status_updater", BCON_BOOL(true),
        "status_update_ip", BCON_BOOL(true)
        );

    bson_t *query = BCON_NEW(
        "the_id", BCON_BINARY(comment_id, UUIDLEN)
    );
    bson_t *result = NULL;

    error = db_find_one(MONGO_COMMENT, query, fields, &result);
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


TEST(pttdb_comment, serialize_comment_bson) {
    Comment comment = {};
    Comment comment2 = {};

    init_comment_buf(&comment);
    init_comment_buf(&comment2);

    comment.version = 2;
    gen_uuid(comment.the_id);
    gen_uuid(comment.main_id);
    comment.status = LIVE_STATUS_ALIVE;

    strcpy(comment.status_updater, "updater1");
    strcpy(comment.status_update_ip, "10.3.1.4");

    comment.comment_type = COMMENT_TYPE_GOOD;
    comment.karma = KARMA_GOOD;

    strcpy(comment.poster, "poster1");
    strcpy(comment.ip, "10.3.1.5");
    comment.create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST
    strcpy(comment.updater, "updater2");
    strcpy(comment.update_ip, "10.3.1.6");
    comment.update_milli_timestamp = 1514764801000; //2018-01-01 08:00:01 CST

    strcpy(comment.buf, "test_buf");
    comment.len = strlen(comment.buf);

    bson_t *comment_bson = NULL;

    Err error = _serialize_comment_bson(&comment, &comment_bson);
    EXPECT_EQ(S_OK, error);

    char *str = bson_as_canonical_extended_json(comment_bson, NULL);
    fprintf(stderr, "test_pttdb_comment.serialize_comment_bson: comment_bson: %s\n", str);
    bson_free(str);

    error = _deserialize_comment_bson(comment_bson, &comment2);

    bson_safe_destroy(&comment_bson);

    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(comment.version, comment2.version);
    EXPECT_EQ(0, strncmp((char *)comment.the_id, (char *)comment2.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)comment.main_id, (char *)comment2.main_id, UUIDLEN));
    EXPECT_EQ(comment.status, comment2.status);
    EXPECT_STREQ(comment.status_updater, comment2.status_updater);
    EXPECT_STREQ(comment.status_update_ip, comment2.status_update_ip);
    EXPECT_EQ(comment.comment_type, comment2.comment_type);
    EXPECT_EQ(comment.karma, comment2.karma);
    EXPECT_STREQ(comment.poster, comment2.poster);
    EXPECT_STREQ(comment.ip, comment2.ip);
    EXPECT_EQ(comment.create_milli_timestamp, comment2.create_milli_timestamp);
    EXPECT_STREQ(comment.updater, comment2.updater);
    EXPECT_STREQ(comment.update_ip, comment2.update_ip);
    EXPECT_EQ(comment.update_milli_timestamp, comment2.update_milli_timestamp);
    EXPECT_EQ(comment.len, comment2.len);
    EXPECT_STREQ(comment.buf, comment2.buf);

    destroy_comment(&comment);
    destroy_comment(&comment2);
}

TEST(pttdb_comment, get_comment_info_by_main) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    UUID main_id;
    UUID comment_id;
    UUID comment_id2;

    gen_uuid(main_id);

    Err error = S_OK;
    error = create_comment(main_id, (char *)"poster1", (char *)"10.3.1.4", 10, (char *)"test1test1", COMMENT_TYPE_GOOD, comment_id);
    EXPECT_EQ(S_OK, error);
    error = create_comment(main_id, (char *)"poster1", (char *)"10.3.1.4", 10, (char *)"test2test2", COMMENT_TYPE_GOOD, comment_id2);
    EXPECT_EQ(S_OK, error);

    int n_total_comments = 0;
    int total_len = 0;
    error = get_comment_info_by_main(main_id, &n_total_comments, &total_len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2, n_total_comments);
    EXPECT_EQ(20, total_len);
}

TEST(pttdb_comment, get_comment_count_by_main) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    UUID main_id;
    UUID comment_id;
    UUID comment_id2;

    gen_uuid(main_id);

    Err error = S_OK;
    error = create_comment(main_id, (char *)"poster1", (char *)"10.3.1.4", 10, (char *)"test1test1", COMMENT_TYPE_GOOD, comment_id);
    EXPECT_EQ(S_OK, error);
    error = create_comment(main_id, (char *)"poster1", (char *)"10.3.1.4", 10, (char *)"test2test2", COMMENT_TYPE_GOOD, comment_id2);
    EXPECT_EQ(S_OK, error);

    int n_total_comments = 0;
    error = get_comment_count_by_main(main_id, &n_total_comments);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2, n_total_comments);
}

TEST(pttdb_comment, ensure_db_results_order) {
    int n_results = 100;
    bson_t **db_results = (bson_t **)malloc(sizeof(bson_t *) * n_results);
    long int rand_int = 0;

    for(int i = 0; i < n_results; i++) {
        rand_int = random();
        db_results[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)"test_poster", 11),
                "create_milli_timestamp", BCON_INT64(rand_int)
            );    
    }

    Err error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GT);
    EXPECT_EQ(S_ERR, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GTE);
    EXPECT_EQ(S_ERR, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LT);
    EXPECT_EQ(S_ERR, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LTE);
    EXPECT_EQ(S_ERR, error);

    for(int i = 0; i < n_results; i++) {
        bson_safe_destroy(&db_results[i]);
    }
    free(db_results);
}

TEST(pttdb_comment, ensure_db_results_order2) {
    int n_results = 100;
    bson_t **db_results = (bson_t **)malloc(sizeof(bson_t *) * n_results);

    for(int i = 0; i < n_results; i++) {
        db_results[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)"test_poster", 11),
                "create_milli_timestamp", BCON_INT64(i)
            );    
    }

    Err error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GTE);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LT);
    EXPECT_EQ(S_ERR, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LTE);
    EXPECT_EQ(S_ERR, error);

    for(int i = 0; i < n_results; i++) {
        bson_safe_destroy(&db_results[i]);
    }
    free(db_results);
}

TEST(pttdb_comment, ensure_db_results_order3) {
    int n_results = 100;
    bson_t **db_results = (bson_t **)malloc(sizeof(bson_t *) * n_results);

    for(int i = 0; i < n_results; i++) {
        db_results[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)"test_poster", 11),
                "create_milli_timestamp", BCON_INT64(100 - i)
            );    
    }

    Err error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LTE);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GT);
    EXPECT_EQ(S_ERR, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GTE);
    EXPECT_EQ(S_ERR, error);

    for(int i = 0; i < n_results; i++) {
        bson_safe_destroy(&db_results[i]);
    }
    free(db_results);
}

TEST(pttdb_comment, ensure_db_results_order4) {
    int n_results = 100;
    bson_t **db_results = (bson_t **)malloc(sizeof(bson_t *) * n_results);

    char poster[IDLEN + 1] = {};
    for(int i = 0; i < n_results; i++) {
        sprintf(poster, "poster%03d", i);
        db_results[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)poster, IDLEN),
                "create_milli_timestamp", BCON_INT64(100)
            );    
    }

    Err error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GTE);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LT);
    EXPECT_EQ(S_ERR, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LTE);
    EXPECT_EQ(S_ERR, error);

    for(int i = 0; i < n_results; i++) {
        bson_safe_destroy(&db_results[i]);
    }
    free(db_results);
}

TEST(pttdb_comment, ensure_db_results_order5) {
    int n_results = 100;
    bson_t **db_results = (bson_t **)malloc(sizeof(bson_t *) * n_results);

    char poster[IDLEN + 1] = {};
    for(int i = 0; i < n_results; i++) {
        sprintf(poster, "poster%03d", 100 - i);
        db_results[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)poster, IDLEN),
                "create_milli_timestamp", BCON_INT64(100)
            );    
    }

    Err error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LTE);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GT);
    EXPECT_EQ(S_ERR, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GTE);
    EXPECT_EQ(S_ERR, error);

    for(int i = 0; i < n_results; i++) {
        bson_safe_destroy(&db_results[i]);
    }
    free(db_results);
}

TEST(pttdb_comment, sort_db_results_order) {
    int n_results = 100;
    bson_t **db_results = (bson_t **)malloc(sizeof(bson_t *) * n_results);
    long int rand_int = 0;

    for(int i = 0; i < n_results; i++) {
        rand_int = random();
        db_results[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)"test_poster", 11),
                "create_milli_timestamp", BCON_INT64(rand_int)
            );    
    }

    Err error = _sort_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LT);
    EXPECT_EQ(S_ERR, error);

    error = _sort_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GTE);
    EXPECT_EQ(S_ERR, error);

    error = _sort_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GTE);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GTE);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LTE);
    EXPECT_EQ(S_ERR, error);

    error = _sort_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LTE);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LTE);
    EXPECT_EQ(S_OK, error);

    for(int i = 0; i < n_results; i++) {
        bson_safe_destroy(&db_results[i]);
    }
    free(db_results);
}

TEST(pttdb_comment, sort_db_results_order2) {
    int n_results = 100;
    bson_t **db_results = (bson_t **)malloc(sizeof(bson_t *) * n_results);
    long int rand_int = 0;

    char poster[IDLEN + 1] = {};
    for(int i = 0; i < n_results; i++) {
        rand_int = random();
        sprintf(poster, "poster%03ld", rand_int);
        db_results[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)poster, IDLEN),
                "create_milli_timestamp", BCON_INT64(100)
            );    
    }

    Err error = _sort_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LT);
    EXPECT_EQ(S_ERR, error);

    error = _sort_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LT);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GTE);
    EXPECT_EQ(S_ERR, error);

    error = _sort_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GTE);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_GTE);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LTE);
    EXPECT_EQ(S_ERR, error);

    error = _sort_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LTE);
    EXPECT_EQ(S_OK, error);

    error = _ensure_db_results_order(db_results, n_results, READ_COMMENTS_OP_TYPE_LTE);
    EXPECT_EQ(S_OK, error);

    for(int i = 0; i < n_results; i++) {
        bson_safe_destroy(&db_results[i]);
    }
    free(db_results);
}

TEST(pttdb_comment, read_comments_by_main)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id);

    char poster[IDLEN + 1] = {};
    int n_comments = 100;
    for(int i = 0; i < n_comments; i++) {
        sprintf(poster, "poster%03d", i);
        error = create_comment(main_id, poster, (char *)"10.1.1.4", 10, (char *)"test1test1", COMMENT_TYPE_GOOD, comment_id);
        EXPECT_EQ(S_OK, error);
    }

    int len;
    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    for(int i = 0; i < 10; i++) {
        sprintf(poster, "poster%03d", i);
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main: (%d/%d) (%lld/%s)\n", i, 10, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    for(int i = 0; i < 100; i++) {
        destroy_comment(&comments[i]);
    }
}

TEST(pttdb_comment, read_comments_by_main2)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id);

    char poster[IDLEN + 1] = {};
    int n_comments = 100;
    for(int i = 0; i < n_comments; i++) {
        sprintf(poster, "poster%03d", i);
        error = create_comment(main_id, poster, (char *)"10.1.1.4", 10, (char *)"test1test1", COMMENT_TYPE_GOOD, comment_id);
        EXPECT_EQ(S_OK, error);
    }

    int len;
    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    for(int i = 0; i < 10; i++) {
        sprintf(poster, "poster%03d", i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    error = read_comments_by_main(main_id, comments[9].create_milli_timestamp, comments[9].poster, READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments + 10, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    for(int i = 10; i < 20; i++) {
        sprintf(poster, "poster%03d", i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    for(int i = 0; i < 100; i++) {
        destroy_comment(&comments[i]);
    }
}

TEST(pttdb_comment, read_comments_by_main3)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id);

    int n_comments = 100;
    for(int i = 0; i < n_comments; i++) {
        error = create_comment(main_id, (char *)"poster", (char *)"10.1.1.4", 10, (char *)"test1test1", COMMENT_TYPE_GOOD, comment_id);
        usleep(1000);
        EXPECT_EQ(S_OK, error);
    }

    int len;
    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    time64_t first_create_milli_timestamp = comments[0].create_milli_timestamp;
    for(int i = 0; i < 10; i++) {
        EXPECT_LE(first_create_milli_timestamp + i, comments[i].create_milli_timestamp);
        EXPECT_GT(first_create_milli_timestamp + i + 100, comments[i].create_milli_timestamp);
    }

    for(int i = 0; i < 100; i++) {
        destroy_comment(&comments[i]);
    }
}

TEST(pttdb_comment, read_comments_by_main4)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id);

    Comment comment = {};
    init_comment_buf(&comment);
    // comment
    memcpy(comment.main_id, main_id, sizeof(UUID));
    comment.status = LIVE_STATUS_ALIVE;
    strcpy(comment.status_updater, "poster000");
    strcpy(comment.status_update_ip, "10.1.1.4");

    time64_t create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST

    comment.comment_type = COMMENT_TYPE_GOOD;
    comment.karma = KARMA_BY_COMMENT_TYPE[COMMENT_TYPE_GOOD];

    strcpy(comment.poster, "poster000");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comments = 100;
    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < n_comments; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get comments
    int len;
    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    char poster[20] = {};
    for(int i = 0; i < 10; i++) {
        EXPECT_EQ(create_milli_timestamp, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // free
    destroy_comment(&comment);
    for(int i = 0; i < 100; i++) {
        destroy_comment(&comments[i]);
    }
}

TEST(pttdb_comment, read_comments_by_main5_GT)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id);

    Comment comment = {};
    init_comment_buf(&comment);
    // comment
    memcpy(comment.main_id, main_id, sizeof(UUID));
    comment.status = LIVE_STATUS_ALIVE;
    strcpy(comment.status_updater, "poster000");
    strcpy(comment.status_update_ip, "10.1.1.4");

    time64_t create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST

    comment.comment_type = COMMENT_TYPE_GOOD;
    comment.karma = KARMA_BY_COMMENT_TYPE[COMMENT_TYPE_GOOD];

    strcpy(comment.poster, "poster000");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comments = 100;
    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < n_comments; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get comments
    int len;
    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    char poster[20] = {};
    for(int i = 0; i < 10; i++) {
        EXPECT_EQ(create_milli_timestamp, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // get comments
    error = read_comments_by_main(main_id, comments[9].create_milli_timestamp, comments[9].poster, READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments + 10, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    for(int i = 10; i < 15; i++) {
        EXPECT_EQ(create_milli_timestamp, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    for(int i = 15; i < 20; i++) {
        EXPECT_EQ(create_milli_timestamp + i, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // free
    destroy_comment(&comment);
    for(int i = 0; i < 100; i++) {
        destroy_comment(&comments[i]);
    }
}

TEST(pttdb_comment, read_comments_by_main5_GTE)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id);

    Comment comment = {};
    init_comment_buf(&comment);
    // comment
    memcpy(comment.main_id, main_id, sizeof(UUID));
    comment.status = LIVE_STATUS_ALIVE;
    strcpy(comment.status_updater, "poster000");
    strcpy(comment.status_update_ip, "10.1.1.4");

    time64_t create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST

    comment.comment_type = COMMENT_TYPE_GOOD;
    comment.karma = KARMA_BY_COMMENT_TYPE[COMMENT_TYPE_GOOD];

    strcpy(comment.poster, "poster000");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comments = 100;
    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < n_comments; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get comments
    int len;
    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GTE, 10, MONGO_COMMENT, comments, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    char poster[20] = {};
    for(int i = 0; i < 10; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_GTE: after 1st read-comments-by-main: (%d/%d) (%lld/%s)\n", i, 10, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // get comments
    error = read_comments_by_main(main_id, comments[9].create_milli_timestamp, comments[9].poster, READ_COMMENTS_OP_TYPE_GTE, 10, MONGO_COMMENT, comments + 10, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    for(int i = 10; i < 16; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_GTE: after 2nd read-comments-by-main: (%d/%d) (%lld/%s)\n", i, 16, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", i - 1);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    for(int i = 16; i < 20; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_GTE: after 2nd read-comments-by-main: (%d/%d) (%lld/%s)\n", i, 20, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp + i - 1, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", i - 1);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // free
    destroy_comment(&comment);
    for(int i = 0; i < 100; i++) {
        destroy_comment(&comments[i]);
    }
}

TEST(pttdb_comment, read_comments_by_main5_LT)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id);

    Comment comment = {};
    init_comment_buf(&comment);
    // comment
    memcpy(comment.main_id, main_id, sizeof(UUID));
    comment.status = LIVE_STATUS_ALIVE;
    strcpy(comment.status_updater, "poster000");
    strcpy(comment.status_update_ip, "10.1.1.4");

    time64_t create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST

    comment.comment_type = COMMENT_TYPE_GOOD;
    comment.karma = KARMA_BY_COMMENT_TYPE[COMMENT_TYPE_GOOD];

    strcpy(comment.poster, "poster000");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comments = 100;
    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get comments
    int len;
    time64_t future_milli_timestamp = 0;
    error = get_milli_timestamp(&future_milli_timestamp);
    future_milli_timestamp += 10000;

    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, future_milli_timestamp, (char *)"", READ_COMMENTS_OP_TYPE_LT, 10, MONGO_COMMENT, comments, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    char poster[20] = {};
    for(int i = 0; i < 10; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LT: after 1st read-comments-by-main: (%d/%d) (%lld/%s)\n", i, 10, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp + 85, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", 90 + i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // get comments
    error = read_comments_by_main(main_id, comments[0].create_milli_timestamp, comments[0].poster, READ_COMMENTS_OP_TYPE_LT, 10, MONGO_COMMENT, comments + 10, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    for(int i = 10; i < 15; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LT: after 2st read-comments-by-main: (%d/%d) (%lld/%s)\n", i, 15, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp + 70 + i, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", 70 + i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    for(int i = 15; i < 20; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LT: after 2st read-comments-by-main: (%d/%d) (%lld/%s)\n", i, 20, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp + 85, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", 70 + i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // free
    destroy_comment(&comment);
    for(int i = 0; i < 100; i++) {
        destroy_comment(&comments[i]);
    }
}

TEST(pttdb_comment, read_comments_by_main5_LTE)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id);

    Comment comment = {};
    init_comment_buf(&comment);
    // comment
    memcpy(comment.main_id, main_id, sizeof(UUID));
    comment.status = LIVE_STATUS_ALIVE;
    strcpy(comment.status_updater, "poster000");
    strcpy(comment.status_update_ip, "10.1.1.4");

    time64_t create_milli_timestamp = 1514764800000; //2018-01-01 08:00:00 CST

    comment.comment_type = COMMENT_TYPE_GOOD;
    comment.karma = KARMA_BY_COMMENT_TYPE[COMMENT_TYPE_GOOD];

    strcpy(comment.poster, "poster000");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comments = 100;
    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = _serialize_comment_bson(&comment, &comment_bson);
        error = _serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get comments
    int len;
    time64_t future_milli_timestamp = 0;
    error = get_milli_timestamp(&future_milli_timestamp);
    future_milli_timestamp += 10000;

    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, future_milli_timestamp, (char *)"", READ_COMMENTS_OP_TYPE_LTE, 10, MONGO_COMMENT, comments, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    char poster[20] = {};
    for(int i = 0; i < 10; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LTE: after 1st read-comments-by-main: (%d/%d) (%lld/%s)\n", i, 10, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp + 85, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", 90 + i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // get comments
    error = read_comments_by_main(main_id, comments[0].create_milli_timestamp, comments[0].poster, READ_COMMENTS_OP_TYPE_LTE, 10, MONGO_COMMENT, comments + 10, &n_comments, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comments);
    EXPECT_EQ(100, len);
    for(int i = 10; i < 14; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LTE: after 2st read-comments-by-main: (%d/%d) (%lld/%s)\n", i, 14, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp + 70 + i + 1, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", 70 + i + 1);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    for(int i = 14; i < 20; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LTE: after 2st read-comments-by-main: (%d/%d) (%lld/%s)\n", i, 20, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp + 85, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", 70 + i + 1);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // free
    destroy_comment(&comment);
    for(int i = 0; i < 100; i++) {
        destroy_comment(&comments[i]);
    }
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

    FD = open("log.test_pttdb_comment.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
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
