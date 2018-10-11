#include "gtest/gtest.h"
#include "test.h"
#include "cmpttdb/pttdb_comment.h"
#include "cmpttdb/pttdb_comment_private.h"
#include "cmpttdb/pttdb_main.h"
#include "cmpttdb/pttdb_comment_reply.h"

TEST(pttdb_comment, create_comment) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    UUID main_id;
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char content[] = "temp_content";
    int len = strlen(content);
    enum CommentType comment_type = COMMENT_TYPE_GOOD;

    UUID comment_id;
    UUID tmp_comment_id;

    gen_uuid(main_id, 0);
    Err error_code = create_comment(main_id, poster, ip, len, content, comment_type, comment_id, 0);
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

    UUID empty_uuid = {};

    EXPECT_EQ(len, comment.len);
    EXPECT_STREQ(content, comment.buf);
    EXPECT_EQ(0, strncmp((char *)empty_uuid, (char *)comment.comment_reply_id, UUIDLEN));
    EXPECT_EQ(0, comment.n_comment_reply_line);

    destroy_comment(&comment);
}

TEST(pttdb_comment, delete_comment) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    UUID main_id;
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char content[] = "temp_content";
    int len = strlen(content);
    enum CommentType comment_type = COMMENT_TYPE_GOOD;

    UUID comment_id;

    gen_uuid(main_id, 0);
    Err error = create_comment(main_id, poster, ip, len, content, comment_type, comment_id, 0);
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
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    Comment comment = {};
    Comment comment2 = {};

    init_comment_buf(&comment);
    init_comment_buf(&comment2);

    comment.version = 2;
    gen_uuid(comment.the_id, 0);
    gen_uuid(comment.main_id, 0);
    gen_uuid(comment.comment_reply_id, 0);
    comment.n_comment_reply_line = 10;
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

    Err error = serialize_comment_bson(&comment, &comment_bson);
    EXPECT_EQ(S_OK, error);

    char *str = bson_as_canonical_extended_json(comment_bson, NULL);
    fprintf(stderr, "test_pttdb_comment.serialize_comment_bson: comment_bson: %s\n", str);
    bson_free(str);

    error = deserialize_comment_bson(comment_bson, &comment2);

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
    EXPECT_EQ(0, strncmp((char *)comment.comment_reply_id, (char *)comment2.comment_reply_id, UUIDLEN));
    EXPECT_EQ(comment.n_comment_reply_line, comment2.n_comment_reply_line);

    destroy_comment(&comment);
    destroy_comment(&comment2);
}

TEST(pttdb_comment, get_comment_info_by_main) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    UUID main_id;
    UUID comment_id;
    UUID comment_id2;

    gen_uuid(main_id, 0);

    Err error = S_OK;
    error = create_comment(main_id, (char *)"poster1", (char *)"10.3.1.4", 10, (char *)"test1test1", COMMENT_TYPE_GOOD, comment_id, 0);
    EXPECT_EQ(S_OK, error);
    error = create_comment(main_id, (char *)"poster1", (char *)"10.3.1.4", 10, (char *)"test2test2", COMMENT_TYPE_GOOD, comment_id2, 0);
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

    gen_uuid(main_id, 0);

    Err error = S_OK;
    error = create_comment(main_id, (char *)"poster1", (char *)"10.3.1.4", 10, (char *)"test1test1", COMMENT_TYPE_GOOD, comment_id, 0);
    EXPECT_EQ(S_OK, error);
    error = create_comment(main_id, (char *)"poster1", (char *)"10.3.1.4", 10, (char *)"test2test2", COMMENT_TYPE_GOOD, comment_id2, 0);
    EXPECT_EQ(S_OK, error);

    int n_total_comments = 0;
    error = get_comment_count_by_main(main_id, &n_total_comments);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2, n_total_comments);
}

TEST(pttdb_comment, ensure_b_comments_order) {
    int n_comment = 100;
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_comment);
    long int rand_int = 0;

    for(int i = 0; i < n_comment; i++) {
        rand_int = random();
        b_comments[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)"test_poster", 11),
                "create_milli_timestamp", BCON_INT64(rand_int)
            );    
    }

    bool is_good_b_comments_order = false;

    // ASC
    Err error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_good_b_comments_order);

    error = ensure_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    EXPECT_EQ(S_OK, error);
    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    // DESC
    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_good_b_comments_order);

    error = ensure_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC);
    EXPECT_EQ(S_OK, error);
    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    // free
    safe_free_b_list(&b_comments, n_comment);
}

TEST(pttdb_comment, is_b_comments_order2) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    int n_comment = 100;
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_comment);

    for(int i = 0; i < n_comment; i++) {
        b_comments[i] = BCON_NEW(
            "poster", BCON_BINARY((unsigned char *)"test_poster", 11),
            "create_milli_timestamp", BCON_INT64(i)
            );    
    }

    bool is_good_b_comments_order = false;
    Err error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_good_b_comments_order);

    // free
    safe_free_b_list(&b_comments, n_comment);
}


TEST(pttdb_comment, ensure_b_comments_order2) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    int n_comment = 100;
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_comment);

    for(int i = 0; i < n_comment; i++) {
        b_comments[i] = BCON_NEW(
            "poster", BCON_BINARY((unsigned char *)"test_poster", 11),
            "create_milli_timestamp", BCON_INT64(i)
            );    
    }

    Err error = ensure_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    EXPECT_EQ(S_OK, error);

    error = ensure_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC);
    EXPECT_EQ(S_OK, error);

    // free
    safe_free_b_list(&b_comments, n_comment);
}

TEST(pttdb_comment, ensure_b_comments_order3) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    int n_comment = 100;
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_comment);

    for(int i = 0; i < n_comment; i++) {
        b_comments[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)"test_poster", 11),
                "create_milli_timestamp", BCON_INT64(100 - i)
            );    
    }

    Err error = ensure_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC);
    EXPECT_EQ(S_OK, error);

    error = ensure_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    EXPECT_EQ(S_OK, error);

    // free
    safe_free_b_list(&b_comments, n_comment);
}

TEST(pttdb_comment, is_b_comments_order3) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    int n_comment = 100;
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_comment);

    for(int i = 0; i < n_comment; i++) {
        b_comments[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)"test_poster", 11),
                "create_milli_timestamp", BCON_INT64(100 - i)
            );    
    }

    bool is_good_b_comments_order = false;
    Err error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_good_b_comments_order);

    // free
    safe_free_b_list(&b_comments, n_comment);
}

TEST(pttdb_comment, is_b_comments_order4) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    int n_comment = 100;
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_comment);

    char poster[IDLEN + 1] = {};
    for(int i = 0; i < n_comment; i++) {
        sprintf(poster, "poster%03d", i);
        b_comments[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)poster, IDLEN),
                "create_milli_timestamp", BCON_INT64(100)
            );    
    }

    bool is_good_b_comments_order = false;
    Err error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_good_b_comments_order);

    // free
    safe_free_b_list(&b_comments, n_comment);
}

TEST(pttdb_comment, ensure_b_comments_order4) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    int n_comment = 100;
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_comment);

    char poster[IDLEN + 1] = {};
    for(int i = 0; i < n_comment; i++) {
        sprintf(poster, "poster%03d", i);
        b_comments[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)poster, IDLEN),
                "create_milli_timestamp", BCON_INT64(100)
            );    
    }

    Err error = ensure_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    EXPECT_EQ(S_OK, error);

    error = ensure_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC);
    EXPECT_EQ(S_OK, error);

    // free
    safe_free_b_list(&b_comments, n_comment);
}

TEST(pttdb_comment, ensure_b_comments_order5) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    int n_comment = 100;
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_comment);

    char poster[IDLEN + 1] = {};
    for(int i = 0; i < n_comment; i++) {
        sprintf(poster, "poster%03d", 100 - i);
        b_comments[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)poster, IDLEN),
                "create_milli_timestamp", BCON_INT64(100)
            );    
    }

    Err error = ensure_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC);
    EXPECT_EQ(S_OK, error);

    error = ensure_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    EXPECT_EQ(S_OK, error);

    // free
    safe_free_b_list(&b_comments, n_comment);
}

TEST(pttdb_comment, sort_b_comments_order) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    int n_comment = 100;
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_comment);
    long int rand_int = 0;

    int *rand_list = NULL;
    fprintf(stderr, "test_pttdb_comment.sort_b_comments_order: to form rand list\n");
    Err error = form_rand_list(100, &rand_list);
    EXPECT_EQ(S_OK, error);

    for(int i = 0; i < 100; i++) {
        fprintf(stderr, "test_pttdb_comment.sort_b_comments_order: (%d/%d): %d\n", i, 100, rand_list[i]);
    }

    int sum = 0;
    for(int i = 0; i < 100; i++) {
        sum += rand_list[i];
    }
    EXPECT_EQ(4950, sum);

    char poster[IDLEN + 1] = {};
    sprintf(poster, "test_poster");

    for(int i = 0; i < n_comment; i++) {
        rand_int = rand_list[i];
        b_comments[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)"test_poser", IDLEN),
                "create_milli_timestamp", BCON_INT64(rand_int)
            );    
    }

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    EXPECT_EQ(S_OK, error);

    bool is_good_b_comments_order = false;
    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_good_b_comments_order);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC);
    EXPECT_EQ(S_OK, error);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_good_b_comments_order);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    EXPECT_EQ(S_OK, error);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_good_b_comments_order);

    // free
    safe_free_b_list(&b_comments, n_comment);
    safe_free((void **)&rand_list);
}

TEST(pttdb_comment, sort_b_comments_order2) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    int n_comment = 100;
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_comment);
    long int rand_int = 0;

    int *rand_list = NULL;

    fprintf(stderr, "test_pttdb_comment.sort_b_comments_order2: to form rand list\n");
    Err error = form_rand_list(100, &rand_list);
    EXPECT_EQ(S_OK, error);

    for(int i = 0; i < 100; i++) {
        fprintf(stderr, "test_pttdb_comment.sort_b_comments_order: (%d/%d): %d\n", i, 100, rand_list[i]);
    }

    int sum = 0;
    for(int i = 0; i < 100; i++) {
        sum += rand_list[i];
    }
    EXPECT_EQ(4950, sum);


    char *str = NULL;
    char poster[IDLEN + 1] = {};
    for(int i = 0; i < n_comment; i++) {
        rand_int = rand_list[i];
        sprintf(poster, "poster%03ld", rand_int);
        b_comments[i] = BCON_NEW(
                "poster", BCON_BINARY((unsigned char *)poster, IDLEN),
                "create_milli_timestamp", BCON_INT64(100)
            );

        if(!b_comments[i]) {
            n_comment = i;
            fprintf(stderr, "test_pttdb_comment.sort_b_comments_order2: unable to allocate b_comments\n");
            break;
        }
        fprintf(stderr, "test_pttdb_comment.cc.sort_b_comments_order2: to bson_as_canonical_extended_json poster: %s\n", poster);

        str = bson_as_canonical_extended_json(b_comments[i], NULL);
        fprintf(stderr, "test_pttdb_comment.sort_b_comments_order2: (%d/%d) b_comments: %s\n", i, n_comment, str);
        bson_free(str);
    }

    fprintf(stderr, "test_pttdb_comment.sort_b_comments_order2: to sort_b_comments_order: long: %lu long int: %lu long long int: %lu\n", sizeof(long), sizeof(long int), sizeof(long long int));

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    EXPECT_EQ(S_OK, error);

    fprintf(stderr, "test_pttdb_comment.sort_b_comments_order2: after b_comments_order\n");

    bool is_good_b_comments_order = false;
    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    fprintf(stderr, "test_pttdb_comment.sort_b_comments_order2: after ensure b comments order\n");

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_good_b_comments_order);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC);
    EXPECT_EQ(S_OK, error);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_good_b_comments_order);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    EXPECT_EQ(S_OK, error);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_good_b_comments_order);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC);
    EXPECT_EQ(S_OK, error);

    error = is_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_DESC, &is_good_b_comments_order);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_good_b_comments_order);

    fprintf(stderr, "test_pttdb_comment.sort_b_comments_order2: to safe_free_b_list\n");

    // free
    safe_free_b_list(&b_comments, n_comment);

    safe_free((void **)&rand_list);
}

TEST(pttdb_comment, read_comments_by_main)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

    char poster[IDLEN + 1] = {};
    int n_comment = 100;
    for(int i = 0; i < n_comment; i++) {
        sprintf(poster, "poster%03d", i);
        error = create_comment(main_id, poster, (char *)"10.1.1.4", 10, (char *)"test1test1", COMMENT_TYPE_GOOD, comment_id, 0);
        EXPECT_EQ(S_OK, error);
    }

    int len;
    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
    EXPECT_EQ(100, len);
    for(int i = 0; i < 10; i++) {
        sprintf(poster, "poster%03d", i);
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main: (%d/%d) (%ld/%s)\n", i, 10, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    for(int i = 0; i < 100; i++) {
        destroy_comment(&comments[i]);
    }
}

TEST(pttdb_comment, read_comments_by_main2)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

    char poster[IDLEN + 1] = {};
    int n_comment = 100;
    for(int i = 0; i < n_comment; i++) {
        sprintf(poster, "poster%03d", i);
        error = create_comment(main_id, poster, (char *)"10.1.1.4", 10, (char *)"test1test1", COMMENT_TYPE_GOOD, comment_id, 0);
        EXPECT_EQ(S_OK, error);
    }

    int len;
    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
    EXPECT_EQ(100, len);
    for(int i = 0; i < 10; i++) {
        sprintf(poster, "poster%03d", i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    error = read_comments_by_main(main_id, comments[9].create_milli_timestamp, comments[9].poster, READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments + 10, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
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
    gen_uuid(main_id, 0);

    int n_comment = 100;
    for(int i = 0; i < n_comment; i++) {
        error = create_comment(main_id, (char *)"poster", (char *)"10.1.1.4", 10, (char *)"test1test1", COMMENT_TYPE_GOOD, comment_id, 0);
        usleep(1000);
        EXPECT_EQ(S_OK, error);
    }

    int len;
    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
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
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    int n_comment = 100;
    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < n_comment; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

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
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
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
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    int n_comment = 100;
    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < n_comment; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
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
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
    EXPECT_EQ(100, len);
    char poster[20] = {};
    for(int i = 0; i < 10; i++) {
        EXPECT_EQ(create_milli_timestamp, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // get comments
    error = read_comments_by_main(main_id, comments[9].create_milli_timestamp, comments[9].poster, READ_COMMENTS_OP_TYPE_GT, 10, MONGO_COMMENT, comments + 10, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
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
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    int n_comment = 100;
    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < n_comment; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
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
    error = read_comments_by_main(main_id, 0, (char *)"", READ_COMMENTS_OP_TYPE_GTE, 10, MONGO_COMMENT, comments, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
    EXPECT_EQ(100, len);
    char poster[20] = {};
    for(int i = 0; i < 10; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_GTE: after 1st read-comments-by-main: (%d/%d) (%ld/%s)\n", i, 10, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // get comments
    error = read_comments_by_main(main_id, comments[9].create_milli_timestamp, comments[9].poster, READ_COMMENTS_OP_TYPE_GTE, 10, MONGO_COMMENT, comments + 10, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
    EXPECT_EQ(100, len);
    for(int i = 10; i < 16; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_GTE: after 2nd read-comments-by-main: (%d/%d) (%ld/%s)\n", i, 16, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", i - 1);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    for(int i = 16; i < 20; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_GTE: after 2nd read-comments-by-main: (%d/%d) (%ld/%s)\n", i, 20, comments[i].create_milli_timestamp, comments[i].poster);
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
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    int n_comment = 100;
    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get comments
    int len;
    time64_t future_milli_timestamp = MAX_CREATE_MILLI_TIMESTAMP;

    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, future_milli_timestamp, (char *)"", READ_COMMENTS_OP_TYPE_LT, 10, MONGO_COMMENT, comments, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
    EXPECT_EQ(100, len);
    char poster[20] = {};
    for(int i = 0; i < 10; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LT: after 1st read-comments-by-main: (%d/%d) (%ld/%s)\n", i, 10, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp + 85, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", 90 + i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // get comments
    error = read_comments_by_main(main_id, comments[0].create_milli_timestamp, comments[0].poster, READ_COMMENTS_OP_TYPE_LT, 10, MONGO_COMMENT, comments + 10, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
    EXPECT_EQ(100, len);
    for(int i = 10; i < 15; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LT: after 2st read-comments-by-main: (%d/%d) (%ld/%s)\n", i, 15, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp + 70 + i, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", 70 + i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    for(int i = 15; i < 20; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LT: after 2st read-comments-by-main: (%d/%d) (%ld/%s)\n", i, 20, comments[i].create_milli_timestamp, comments[i].poster);
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
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0 );

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

    int n_comment = 100;
    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get comments
    int len;
    time64_t future_milli_timestamp = MAX_CREATE_MILLI_TIMESTAMP;

    Comment comments[100] = {};
    for(int i = 0; i < 100; i++) {
        init_comment_buf(&comments[i]);
    }
    error = read_comments_by_main(main_id, future_milli_timestamp, (char *)"", READ_COMMENTS_OP_TYPE_LTE, 10, MONGO_COMMENT, comments, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
    EXPECT_EQ(100, len);
    char poster[20] = {};
    for(int i = 0; i < 10; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LTE: after 1st read-comments-by-main: (%d/%d) (%ld/%s)\n", i, 10, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp + 85, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", 90 + i);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    // get comments
    error = read_comments_by_main(main_id, comments[0].create_milli_timestamp, comments[0].poster, READ_COMMENTS_OP_TYPE_LTE, 10, MONGO_COMMENT, comments + 10, &n_comment, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_comment);
    EXPECT_EQ(100, len);
    for(int i = 10; i < 14; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LTE: after 2st read-comments-by-main: (%d/%d) (%ld/%s)\n", i, 14, comments[i].create_milli_timestamp, comments[i].poster);
        EXPECT_EQ(create_milli_timestamp + 70 + i + 1, comments[i].create_milli_timestamp);
        sprintf(poster, "poster%03d", 70 + i + 1);
        EXPECT_STREQ(poster, comments[i].poster);
    }

    for(int i = 14; i < 20; i++) {
        fprintf(stderr, "test_pttdb_comment.read_comments_by_main5_LTE: after 2st read-comments-by-main: (%d/%d) (%ld/%s)\n", i, 20, comments[i].create_milli_timestamp, comments[i].poster);
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

TEST(pttdb_comment, get_newest_comment) {
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    int fd = open("data_test/test1.txt", O_RDONLY);

    aidu_t aid = 12345;
    char board[IDLEN + 1] = {};
    char title[TTLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char origin[MAX_ORIGIN_LEN + 1] = {};
    char web_link[MAX_WEB_LINK_LEN + 1] = {};
    int len = 10020;
    UUID main_id = {};
    UUID content_id = {};

    strcpy(board, "test_board");
    strcpy(title, "test_title");
    strcpy(poster, "test_poster");
    strcpy(ip, "test_ip");
    strcpy(origin, "ptt.cc");
    strcpy(web_link, "http://www.ptt.cc/bbs/alonglonglongboard/M.1234567890.ABCD.html");

    // create-main-from-fd
    Err error = create_main_from_fd(aid, board, title, poster, ip, origin, web_link, len, fd, main_id, content_id, 0);
    EXPECT_EQ(S_OK, error);


    close(fd);

    // create-comment
    UUID comment_id = {};
    UUID comment_id2 = {};

    error = create_comment(main_id, (char *)"poster1", (char *)"10.3.1.4", 10, (char *)"test1test1", COMMENT_TYPE_GOOD, comment_id, 0);
    EXPECT_EQ(S_OK, error);
    error = create_comment(main_id, (char *)"poster2", (char *)"10.3.1.4", 10, (char *)"test2test2", COMMENT_TYPE_GOOD, comment_id2, 0);


    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    int n_comment = 0;

    fprintf(stderr, "pttdb_comment.get_newest_comment: to get newest comment: start\n");
    // get newest comment
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_STREQ((char *)"poster2", newest_poster);
    EXPECT_EQ(2, n_comment);
}

TEST(pttdb_comment, get_newest_comment2)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comment = 100;

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }


    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get newest_comments

    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    n_comment = 0;
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment);
    EXPECT_EQ(create_milli_timestamp + 85, newest_create_milli_timestamp);
    EXPECT_STREQ((char *)"poster099", newest_poster);

    // free
    destroy_comment(&comment);
}

TEST(pttdb_comment, read_comments_until_newest_to_bsons)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comment = 100;

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get newest_comments

    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    int n_expected_comment = 0;
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_expected_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_expected_comment);
    EXPECT_EQ(create_milli_timestamp + 85, newest_create_milli_timestamp);
    EXPECT_STREQ((char *)"poster099", newest_poster);

    // insert more comments
    for(int i = 100; i < 102; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // read comments until newest to bsons
    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );

    fprintf(stderr, "test_pttdb_comment.read_comments_util_newest_to_bson: to read_comments_until_newest_to_bsons\n");
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_expected_comment);
    n_comment = 0;
    error = read_comments_until_newest_to_bsons(main_id, newest_create_milli_timestamp, newest_poster, fields, n_expected_comment, b_comments, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);

    char *str = NULL;
    for(int i = 0; i < 100; i++) {
        str = bson_as_canonical_extended_json(b_comments[i], NULL);
        fprintf(stderr, "test_pttdb_comment.read_comments_util_newest_to_bson: (%d/%d): %s\n", i, 100, str);
        bson_free(str);
    }


    time64_t result_create_milli_timestamp = 0;
    char expected_poster[IDLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    int len = 0;
    for(int i = 0; i < 15; i++) {                
        sprintf(expected_poster, "poster%03d", i);
        error = bson_get_value_int64(b_comments[i], (char *)"create_milli_timestamp", (long *)&result_create_milli_timestamp);
        EXPECT_EQ(S_OK, error);
        error = bson_get_value_bin(b_comments[i], (char *)"poster", IDLEN, poster, &len);
        EXPECT_EQ(S_OK, error);
        EXPECT_EQ(create_milli_timestamp, result_create_milli_timestamp);
        EXPECT_STREQ(expected_poster, poster);
    }

    // free
    safe_free_b_list(&b_comments, n_comment);
    bson_safe_destroy(&fields);

    destroy_comment(&comment);
}

TEST(pttdb_comment, sort_b_comments_by_comment_id)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comment = 100;

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get bsons
    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    int n_expected_comment = 0;
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_expected_comment);

    // read comments until newest to bsons
    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );

    fprintf(stderr, "test_pttdb_comment.read_comments_util_newest_to_bson: to read_comments_until_newest_to_bsons\n");
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_expected_comment);
    n_comment = 0;
    error = read_comments_until_newest_to_bsons(main_id, newest_create_milli_timestamp, newest_poster, fields, n_expected_comment, b_comments, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment);

    // sort
    error = sort_b_comments_by_comment_id(b_comments, n_comment);
    EXPECT_EQ(S_OK, error);

    char *str = NULL;
    for(int i = 0; i < 100; i++) {
        str = bson_as_canonical_extended_json(b_comments[i], NULL);
        fprintf(stderr, "test_pttdb_comment.sort_b_comments_by_comment_id: (%d/%d) %s\n", i, 100, str);
        bson_free(str);
    }

    bson_t **p_b_comments = b_comments;
    bson_t **p_next_b_comments = b_comments + 1;
    UUID next_comment_id = {};
    int len = 0;
    for(int i = 0; i < 99; i++, p_b_comments++, p_next_b_comments++) {
        error = bson_get_value_bin(*p_b_comments, (char *)"the_id", UUIDLEN, (char *)comment_id, &len);
        EXPECT_EQ(S_OK, error);
        error = bson_get_value_bin(*p_next_b_comments, (char *)"the_id", UUIDLEN, (char *)next_comment_id, &len);
        EXPECT_EQ(S_OK, error);
        EXPECT_LT(strncmp((char *)comment_id, (char *)next_comment_id, UUIDLEN), 0);
    }

    //free
    safe_free_b_list(&b_comments, n_comment);
    bson_safe_destroy(&fields);

    destroy_comment(&comment);
}

TEST(pttdb_comment, extract_b_comments_comment_id_to_bsons_no_comment_reply_ids) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comment = 100;

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get newest_comments

    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    int n_expected_comment = 0;
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_expected_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_expected_comment);
    EXPECT_EQ(create_milli_timestamp + 85, newest_create_milli_timestamp);
    EXPECT_STREQ((char *)"poster099", newest_poster);

    // insert more comments
    for(int i = 100; i < 102; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // read comments until newest to bsons
    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );

    fprintf(stderr, "test_pttdb_comment.read_comments_util_newest_to_bson: to read_comments_until_newest_to_bsons\n");
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_expected_comment);
    n_comment = 0;
    error = read_comments_until_newest_to_bsons(main_id, newest_create_milli_timestamp, newest_poster, fields, n_expected_comment, b_comments, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);

    time64_t result_create_milli_timestamp = 0;
    char expected_poster[IDLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    int len = 0;
    for(int i = 0; i < 15; i++) {                
        sprintf(expected_poster, "poster%03d", i);
        error = bson_get_value_int64(b_comments[i], (char *)"create_milli_timestamp", (long *)&result_create_milli_timestamp);
        EXPECT_EQ(S_OK, error);
        error = bson_get_value_bin(b_comments[i], (char *)"poster", IDLEN, poster, &len);
        EXPECT_EQ(S_OK, error);
        EXPECT_EQ(create_milli_timestamp, result_create_milli_timestamp);
        EXPECT_STREQ(expected_poster, poster);
    }

    // extract_b_comments_comment_id_to_bsons
    bson_t *b_comment_ids = NULL;
    error = extract_b_comments_comment_id_to_bsons(b_comments, n_comment, (char *)"$in", &b_comment_ids);
    EXPECT_EQ(S_OK, error);

    bool is_exist = false;
    error = bson_exists(b_comment_ids, (char *)"$in", &is_exist);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_exist);

    bson_iter_t iter;
    bson_iter_t sub_iter;

    bool is_exist_in_array[100] = {};

    int the_id = 0;
    bson_iter_init_find(&iter, b_comment_ids, "$in");
    bson_iter_recurse(&iter, &sub_iter);
    while(bson_iter_next(&sub_iter)) {
        the_id = atoi(bson_iter_key (&sub_iter));
        is_exist_in_array[the_id] = true;
    }
    for(int i = 0; i < 100; i++) {
        EXPECT_EQ(true, is_exist_in_array[i]);
    }

    // free
    bson_safe_destroy(&b_comment_ids);
    safe_free_b_list(&b_comments, n_comment);
    bson_safe_destroy(&fields);

    destroy_comment(&comment);
}

TEST(pttdb_comment, extract_b_comments_comment_id_to_bsons_no_comment_reply_ids2) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comment = 100;

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get newest_comments

    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    int n_expected_comment = 0;
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_expected_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_expected_comment);
    EXPECT_EQ(create_milli_timestamp + 85, newest_create_milli_timestamp);
    EXPECT_STREQ((char *)"poster099", newest_poster);

    // insert more comments
    for(int i = 100; i < 102; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // read comments until newest to bsons
    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );

    fprintf(stderr, "test_pttdb_comment.read_comments_util_newest_to_bson: to read_comments_until_newest_to_bsons\n");
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_expected_comment);
    n_comment = 0;
    error = read_comments_until_newest_to_bsons(main_id, newest_create_milli_timestamp, newest_poster, fields, n_expected_comment, b_comments, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);

    time64_t result_create_milli_timestamp = 0;
    char expected_poster[IDLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    int len = 0;
    for(int i = 0; i < 15; i++) {                
        sprintf(expected_poster, "poster%03d", i);
        error = bson_get_value_int64(b_comments[i], (char *)"create_milli_timestamp", (long *)&result_create_milli_timestamp);
        EXPECT_EQ(S_OK, error);
        error = bson_get_value_bin(b_comments[i], (char *)"poster", IDLEN, poster, &len);
        EXPECT_EQ(S_OK, error);
        EXPECT_EQ(create_milli_timestamp, result_create_milli_timestamp);
        EXPECT_STREQ(expected_poster, poster);
    }

    // extract_b_comments_comment_id_to_bsons
    bson_t *b_comment_ids = NULL;
    error = extract_b_comments_comment_id_to_bsons(b_comments, n_comment, (char *)"$in", &b_comment_ids);
    EXPECT_EQ(S_OK, error);

    bool is_exist = false;
    error = bson_exists(b_comment_ids, (char *)"$in", &is_exist);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_exist);

    bson_iter_t iter;
    bson_iter_t sub_iter;

    bool is_exist_in_array[100] = {};

    int the_id = 0;
    bson_iter_init_find(&iter, b_comment_ids, "$in");
    bson_iter_recurse(&iter, &sub_iter);
    while(bson_iter_next(&sub_iter)) {
        the_id = atoi(bson_iter_key (&sub_iter));
        is_exist_in_array[the_id] = true;
    }
    for(int i = 0; i < 100; i++) {
        EXPECT_EQ(true, is_exist_in_array[i]);
    }

    // extract_b_comments_comment_id_to_bsons
    bson_t *b_comment_ids2 = NULL;
    error = extract_b_comments_comment_id_to_bsons(b_comments, n_comment, (char *)"$in", &b_comment_ids2);
    EXPECT_EQ(S_OK, error);

    is_exist = false;
    error = bson_exists(b_comment_ids2, (char *)"$in", &is_exist);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_exist);

    bzero(is_exist_in_array, sizeof(is_exist_in_array));

    the_id = 0;
    bson_iter_init_find(&iter, b_comment_ids2, "$in");
    bson_iter_recurse(&iter, &sub_iter);
    while(bson_iter_next(&sub_iter)) {
        the_id = atoi(bson_iter_key (&sub_iter));
        is_exist_in_array[the_id] = true;
    }
    for(int i = 0; i < 100; i++) {
        EXPECT_EQ(true, is_exist_in_array[i]);
    }

    // free
    bson_safe_destroy(&b_comment_ids2);
    bson_safe_destroy(&b_comment_ids);
    safe_free_b_list(&b_comments, n_comment);
    bson_safe_destroy(&fields);

    destroy_comment(&comment);
}

TEST(pttdb_comment, extract_b_comments_comment_reply_id_to_bsons_no_comment_reply_ids) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comment = 100;

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get newest_comments

    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    int n_expected_comment = 0;
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_expected_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_expected_comment);
    EXPECT_EQ(create_milli_timestamp + 85, newest_create_milli_timestamp);
    EXPECT_STREQ((char *)"poster099", newest_poster);

    // insert more comments
    for(int i = 100; i < 102; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // read comments until newest to bsons
    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "comment_reply_id", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );

    fprintf(stderr, "test_pttdb_comment.read_comments_util_newest_to_bson: to read_comments_until_newest_to_bsons\n");
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_expected_comment);
    n_comment = 0;
    error = read_comments_until_newest_to_bsons(main_id, newest_create_milli_timestamp, newest_poster, fields, n_expected_comment, b_comments, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);

    time64_t result_create_milli_timestamp = 0;
    char expected_poster[IDLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    int len = 0;
    for(int i = 0; i < 15; i++) {                
        sprintf(expected_poster, "poster%03d", i);
        error = bson_get_value_int64(b_comments[i], (char *)"create_milli_timestamp", (long *)&result_create_milli_timestamp);
        EXPECT_EQ(S_OK, error);
        error = bson_get_value_bin(b_comments[i], (char *)"poster", IDLEN, poster, &len);
        EXPECT_EQ(S_OK, error);
        EXPECT_EQ(create_milli_timestamp, result_create_milli_timestamp);
        EXPECT_STREQ(expected_poster, poster);
    }

    // extract_b_comments_comment_reply_id_to_bsons
    bson_t *b_comment_reply_ids = NULL;
    int n_comment_reply = 0;
    int n_comment_reply_block = 0;
    error = extract_b_comments_comment_reply_id_to_bsons(b_comments, n_comment, (char *)"$in", &b_comment_reply_ids, &n_comment_reply, &n_comment_reply_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(0, n_comment_reply);

    bool is_exist = false;
    error = bson_exists(b_comment_reply_ids, (char *)"$in", &is_exist);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_exist);

    bson_iter_t iter;
    bson_iter_t sub_iter;

    bool is_exist_in_array[100] = {};

    int the_id = 0;
    bson_iter_init_find(&iter, b_comment_reply_ids, "$in");
    bson_iter_recurse(&iter, &sub_iter);
    while(bson_iter_next(&sub_iter)) {
        the_id = atoi(bson_iter_key (&sub_iter));
        is_exist_in_array[the_id] = true;
    }
    for(int i = 0; i < 100; i++) {
        EXPECT_EQ(false, is_exist_in_array[i]);
    }

    // free
    bson_safe_destroy(&b_comment_reply_ids);
    safe_free_b_list(&b_comments, n_comment);
    bson_safe_destroy(&fields);

    destroy_comment(&comment);
}

TEST(pttdb_comment, extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comment = 100;

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;
        gen_uuid(comment.comment_reply_id, 0);
        comment.n_comment_reply_line = 1;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    bzero(comment.comment_reply_id, UUIDLEN);
    comment.n_comment_reply_line = 0;

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get newest_comments

    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    int n_expected_comment = 0;
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_expected_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_expected_comment);
    EXPECT_EQ(create_milli_timestamp + 85, newest_create_milli_timestamp);
    EXPECT_STREQ((char *)"poster099", newest_poster);

    // insert more comments
    for(int i = 100; i < 102; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // read comments until newest to bsons
    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "comment_reply_id", BCON_BOOL(true),
        "n_comment_reply_block", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );

    fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids: to read_comments_until_newest_to_bsons\n");
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_expected_comment);
    n_comment = 0;
    error = read_comments_until_newest_to_bsons(main_id, newest_create_milli_timestamp, newest_poster, fields, n_expected_comment, b_comments, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    char *str = NULL;
    for(int i = 0; i < n_comment; i++) {
        str = bson_as_canonical_extended_json(b_comments[i], NULL);
        fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids: (%d/%d) %s\n", i, n_comment, str);
        bson_free(str);
    }

    time64_t result_create_milli_timestamp = 0;
    char expected_poster[IDLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    int len = 0;
    for(int i = 0; i < 15; i++) {                
        sprintf(expected_poster, "poster%03d", i);
        error = bson_get_value_int64(b_comments[i], (char *)"create_milli_timestamp", (long *)&result_create_milli_timestamp);
        EXPECT_EQ(S_OK, error);
        error = bson_get_value_bin(b_comments[i], (char *)"poster", IDLEN, poster, &len);
        EXPECT_EQ(S_OK, error);
        EXPECT_EQ(create_milli_timestamp, result_create_milli_timestamp);
        EXPECT_STREQ(expected_poster, poster);
    }

    // extract_b_comments_comment_reply_id_to_bsons
    bson_t *b_comment_reply_ids = NULL;
    int n_comment_reply = 0;
    int n_comment_reply_block = 0;
    error = extract_b_comments_comment_reply_id_to_bsons(b_comments, n_comment, (char *)"$in", &b_comment_reply_ids, &n_comment_reply, &n_comment_reply_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(15, n_comment_reply);

    str = bson_as_canonical_extended_json(b_comment_reply_ids, NULL);
    fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids: b_comment_reply_ids: %s", str);
    bson_free(str);

    bool is_exist = false;
    error = bson_exists(b_comment_reply_ids, (char *)"$in", &is_exist);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_exist);

    bson_iter_t iter;
    bson_iter_t sub_iter;

    bool is_exist_in_array[100] = {};

    int the_id = 0;
    bson_iter_init_find(&iter, b_comment_reply_ids, "$in");
    bson_iter_recurse(&iter, &sub_iter);
    int sub_iter_i = 0;
    while(bson_iter_next(&sub_iter)) {
        the_id = atoi(bson_iter_key (&sub_iter));
        is_exist_in_array[the_id] = true;
        fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids: b_comment_reply_ids: sub_iter_i: %d the_id: %d\n", sub_iter_i, the_id);
        sub_iter_i++;
    }
    for(int i = 0; i < 15; i++) {
        EXPECT_EQ(true, is_exist_in_array[i]);
    }

    for(int i = 15; i < 100; i++) {
        EXPECT_EQ(false, is_exist_in_array[i]);
    }

    // free
    bson_safe_destroy(&b_comment_reply_ids);
    safe_free_b_list(&b_comments, n_comment);
    bson_safe_destroy(&fields);

    destroy_comment(&comment);
}


TEST(pttdb_comment, dynamic_read_b_comment_comment_reply_by_ids_to_buf) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1\r\n");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    char replier[IDLEN + 1] = {};
    int n_comment = 100;

    UUID comment_reply_id = {};

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        sprintf(replier, "reply%03d", i);
        error = create_comment_reply(main_id, comment_id, replier, (char *)"10.1.1.5", 12, (char *)"replyreply\r\n", comment_reply_id, 0);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get newest_comments

    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    int n_expected_comment = 0;
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_expected_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_expected_comment);
    EXPECT_EQ(create_milli_timestamp + 85, newest_create_milli_timestamp);
    EXPECT_STREQ((char *)"poster099", newest_poster);

    // insert more comments
    for(int i = 100; i < 102; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // read comments until newest to bsons
    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "comment_reply_id", BCON_BOOL(true),
        "n_comment_reply_block", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );

    fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids: to read_comments_until_newest_to_bsons\n");
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_expected_comment);
    n_comment = 0;
    error = read_comments_until_newest_to_bsons(main_id, newest_create_milli_timestamp, newest_poster, fields, n_expected_comment, b_comments, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    char *str = NULL;
    for(int i = 0; i < n_comment; i++) {
        str = bson_as_canonical_extended_json(b_comments[i], NULL);
        fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids: (%d/%d) %s\n", i, n_comment, str);
        bson_free(str);
    }

    time64_t result_create_milli_timestamp = 0;
    char expected_poster[IDLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    int len = 0;
    for(int i = 0; i < 15; i++) {                
        sprintf(expected_poster, "poster%03d", i);
        error = bson_get_value_int64(b_comments[i], (char *)"create_milli_timestamp", (long *)&result_create_milli_timestamp);
        EXPECT_EQ(S_OK, error);
        error = bson_get_value_bin(b_comments[i], (char *)"poster", IDLEN, poster, &len);
        EXPECT_EQ(S_OK, error);
        EXPECT_EQ(create_milli_timestamp, result_create_milli_timestamp);
        EXPECT_STREQ(expected_poster, poster);
    }

    // dynamic read b comment comment reply by ids to buf
    char result_buf[MAX_BUF_SIZE] = {};
    int n_read_comment = 0;
    int n_read_comment_reply = 0;
    int len_buf = 0;
    error = dynamic_read_b_comment_comment_reply_by_ids_to_buf(b_comments, n_comment, result_buf, MAX_BUF_SIZE, &n_read_comment, &n_read_comment_reply, &len_buf);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_read_comment);
    EXPECT_EQ(15, n_read_comment_reply);
    EXPECT_EQ(1180, len_buf);

    char expected_result_buf[] = "test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\ntest1test1replyreply\r\n";


    fprintf(stderr, "test_pttdb_comment.dynamic_read_b_comment_comment_reply_by_ids_to_buf: result_buf: %s\n", result_buf);
    EXPECT_STREQ(expected_result_buf, result_buf);

    // free
    safe_free_b_list(&b_comments, n_comment);
    bson_safe_destroy(&fields);

    destroy_comment(&comment);
}

TEST(pttdb_comment, dynamic_read_b_comment_comment_reply_by_ids_to_buf_n_only) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    strcpy(comment.buf, "test1test1");
    comment.len = 10;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    char replier[IDLEN + 1] = {};
    int n_comment = 100;

    UUID comment_reply_id = {};

    for(int i = 85; i < 100; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        sprintf(replier, "reply%03d", i);
        error = create_comment_reply(main_id, comment_id, replier, (char *)"10.1.1.5", 11, (char *)"replyreply\n", comment_reply_id, 0);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + i;
        comment.update_milli_timestamp = create_milli_timestamp + i;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get newest_comments

    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    int n_expected_comment = 0;
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_expected_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_expected_comment);
    EXPECT_EQ(create_milli_timestamp + 85, newest_create_milli_timestamp);
    EXPECT_STREQ((char *)"poster099", newest_poster);

    // insert more comments
    for(int i = 100; i < 102; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // read comments until newest to bsons
    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "comment_reply_id", BCON_BOOL(true),
        "n_comment_reply_block", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );

    fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids: to read_comments_until_newest_to_bsons\n");
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_expected_comment);
    n_comment = 0;
    error = read_comments_until_newest_to_bsons(main_id, newest_create_milli_timestamp, newest_poster, fields, n_expected_comment, b_comments, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    char *str = NULL;
    for(int i = 0; i < n_comment; i++) {
        str = bson_as_canonical_extended_json(b_comments[i], NULL);
        fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids: (%d/%d) %s\n", i, n_comment, str);
        bson_free(str);
    }

    time64_t result_create_milli_timestamp = 0;
    char expected_poster[IDLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    int len = 0;
    for(int i = 0; i < 15; i++) {                
        sprintf(expected_poster, "poster%03d", i);
        error = bson_get_value_int64(b_comments[i], (char *)"create_milli_timestamp", (long *)&result_create_milli_timestamp);
        EXPECT_EQ(S_OK, error);
        error = bson_get_value_bin(b_comments[i], (char *)"poster", IDLEN, poster, &len);
        EXPECT_EQ(S_OK, error);
        EXPECT_EQ(create_milli_timestamp, result_create_milli_timestamp);
        EXPECT_STREQ(expected_poster, poster);
    }

    // dynamic read b comment comment reply by ids to buf
    char result_buf[MAX_BUF_SIZE] = {};
    int n_read_comment = 0;
    int n_read_comment_reply = 0;
    int len_buf = 0;
    error = dynamic_read_b_comment_comment_reply_by_ids_to_buf(b_comments, n_comment, result_buf, MAX_BUF_SIZE, &n_read_comment, &n_read_comment_reply, &len_buf);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_read_comment);
    EXPECT_EQ(15, n_read_comment_reply);
    EXPECT_EQ(1165, len_buf);

    char expected_result_buf[] = "test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\ntest1test1replyreply\n";


    fprintf(stderr, "test_pttdb_comment.dynamic_read_b_comment_comment_reply_by_ids_to_buf: result_buf: %s\n", result_buf);
    EXPECT_STREQ(expected_result_buf, result_buf);

    // free
    safe_free_b_list(&b_comments, n_comment);
    bson_safe_destroy(&fields);

    destroy_comment(&comment);
}

TEST(pttdb_comment, dynamic_read_b_comment_comment_reply_by_ids_to_buf_large_content) {
    fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids_large_content: start\n");

    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);

    fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids_large_content: start2\n");

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    char replier[IDLEN + 1] = {};
    int n_comment = 100;

    UUID comment_reply_id = {};

    for(int i = 85; i < 100; i++) {
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
        error = create_comment_reply(main_id, comment_id, replier, (char *)"10.1.1.5", 12, (char *)"replyreply\r\n", comment_reply_id, 0);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
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

    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        sprintf(comment.buf, "testtest0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789%03d\r\n", i);
        comment.len = strlen(comment.buf);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get newest_comments

    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    int n_expected_comment = 0;
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_expected_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_expected_comment);
    EXPECT_EQ(create_milli_timestamp + 85, newest_create_milli_timestamp);
    EXPECT_STREQ((char *)"poster099", newest_poster);

    // insert more comments
    for(int i = 100; i < 102; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // read comments until newest to bsons
    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "comment_reply_id", BCON_BOOL(true),
        "n_comment_reply_block", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );

    fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids_large_content: to read_comments_until_newest_to_bsons\n");
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_expected_comment);
    n_comment = 0;
    error = read_comments_until_newest_to_bsons(main_id, newest_create_milli_timestamp, newest_poster, fields, n_expected_comment, b_comments, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    char *str = NULL;
    for(int i = 0; i < n_comment; i++) {
        str = bson_as_canonical_extended_json(b_comments[i], NULL);
        fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids_large_content: (%d/%d) %s\n", i, n_comment, str);
        bson_free(str);
    }

    time64_t result_create_milli_timestamp = 0;
    char expected_poster[IDLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    int len = 0;
    for(int i = 0; i < 15; i++) {                
        sprintf(expected_poster, "poster%03d", i);
        error = bson_get_value_int64(b_comments[i], (char *)"create_milli_timestamp", (long *)&result_create_milli_timestamp);
        EXPECT_EQ(S_OK, error);
        error = bson_get_value_bin(b_comments[i], (char *)"poster", IDLEN, poster, &len);
        EXPECT_EQ(S_OK, error);
        EXPECT_EQ(create_milli_timestamp, result_create_milli_timestamp);
        EXPECT_STREQ(expected_poster, poster);
    }

    // dynamic read b comment comment reply by ids to buf
    char result_buf[MAX_BUF_SIZE] = {};
    int n_read_comment = 0;
    int n_read_comment_reply = 0;
    int len_buf = 0;
    error = dynamic_read_b_comment_comment_reply_by_ids_to_buf(b_comments, n_comment, result_buf, MAX_BUF_SIZE, &n_read_comment, &n_read_comment_reply, &len_buf);
    EXPECT_EQ(S_ERR_BUFFER_LEN, error);
    EXPECT_EQ(72, n_read_comment);
    EXPECT_EQ(0, n_read_comment_reply);
    EXPECT_EQ(8136, len_buf);

    // free
    safe_free_b_list(&b_comments, n_comment);
    bson_safe_destroy(&fields);

    destroy_comment(&comment);
}

TEST(pttdb_comment, dynamic_read_b_comment_comment_reply_by_ids_to_buf_large_content_n_only) {
    fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids_large_content: start\n");

    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);

    fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids_large_content: start2\n");

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    gen_uuid(main_id, 0);

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

    strcpy(comment.poster, "poster001");
    strcpy(comment.ip, "10.1.1.4");
    comment.create_milli_timestamp = create_milli_timestamp;
    strcpy(comment.updater, "poster000");
    strcpy(comment.update_ip, "10.1.1.4");
    comment.update_milli_timestamp = create_milli_timestamp;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    char replier[IDLEN + 1] = {};
    int n_comment = 100;

    UUID comment_reply_id = {};

    for(int i = 85; i < 100; i++) {
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
        error = create_comment_reply(main_id, comment_id, replier, (char *)"10.1.1.5", 11, (char *)"replyreply\n", comment_reply_id, 0);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    for(int i = 15; i < 85; i++) {
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

    for(int i = 0; i < 15; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        sprintf(comment.buf, "testtest0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789%03d\n", i);
        comment.len = strlen(comment.buf);

        comment.create_milli_timestamp = create_milli_timestamp;
        comment.update_milli_timestamp = create_milli_timestamp;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);

        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // get newest_comments

    UUID newest_comment_id = {};
    time64_t newest_create_milli_timestamp = 0;
    char newest_poster[IDLEN + 1] = {};
    int n_expected_comment = 0;
    error = get_newest_comment(main_id, newest_comment_id, &newest_create_milli_timestamp, newest_poster, &n_expected_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_expected_comment);
    EXPECT_EQ(create_milli_timestamp + 85, newest_create_milli_timestamp);
    EXPECT_STREQ((char *)"poster099", newest_poster);

    // insert more comments
    for(int i = 100; i < 102; i++) {
        gen_uuid(comment_id, 0);
        memcpy(comment.the_id, comment_id, sizeof(UUID));
        sprintf(comment.poster, "poster%03d", i);

        comment.create_milli_timestamp = create_milli_timestamp + 85;
        comment.update_milli_timestamp = create_milli_timestamp + 85;

        error = serialize_comment_bson(&comment, &comment_bson);
        error = serialize_uuid_bson(comment_id, &comment_id_bson);
        
        error = db_update_one(MONGO_COMMENT, comment_id_bson, comment_bson, true);

        bson_safe_destroy(&comment_bson);
        bson_safe_destroy(&comment_id_bson);

        EXPECT_EQ(S_OK, error);
    }

    // read comments until newest to bsons
    bson_t *fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "the_id", BCON_BOOL(true),
        "comment_reply_id", BCON_BOOL(true),
        "n_comment_reply_block", BCON_BOOL(true),
        "create_milli_timestamp", BCON_BOOL(true),
        "poster", BCON_BOOL(true)
        );

    fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids_large_content: to read_comments_until_newest_to_bsons\n");
    bson_t **b_comments = (bson_t **)malloc(sizeof(bson_t *) * n_expected_comment);
    n_comment = 0;
    error = read_comments_until_newest_to_bsons(main_id, newest_create_milli_timestamp, newest_poster, fields, n_expected_comment, b_comments, &n_comment);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment);

    error = sort_b_comments_order(b_comments, n_comment, READ_COMMENTS_ORDER_TYPE_ASC);
    char *str = NULL;
    for(int i = 0; i < n_comment; i++) {
        str = bson_as_canonical_extended_json(b_comments[i], NULL);
        fprintf(stderr, "test_pttdb_comment.extract_b_comments_comment_reply_id_to_bsons_some_comment_reply_ids_large_content: (%d/%d) %s\n", i, n_comment, str);
        bson_free(str);
    }

    time64_t result_create_milli_timestamp = 0;
    char expected_poster[IDLEN + 1] = {};
    char poster[IDLEN + 1] = {};
    int len = 0;
    for(int i = 0; i < 15; i++) {                
        sprintf(expected_poster, "poster%03d", i);
        error = bson_get_value_int64(b_comments[i], (char *)"create_milli_timestamp", (long *)&result_create_milli_timestamp);
        EXPECT_EQ(S_OK, error);
        error = bson_get_value_bin(b_comments[i], (char *)"poster", IDLEN, poster, &len);
        EXPECT_EQ(S_OK, error);
        EXPECT_EQ(create_milli_timestamp, result_create_milli_timestamp);
        EXPECT_STREQ(expected_poster, poster);
    }

    // dynamic read b comment comment reply by ids to buf
    char result_buf[MAX_BUF_SIZE] = {};
    int n_read_comment = 0;
    int n_read_comment_reply = 0;
    int len_buf = 0;
    error = dynamic_read_b_comment_comment_reply_by_ids_to_buf(b_comments, n_comment, result_buf, MAX_BUF_SIZE, &n_read_comment, &n_read_comment_reply, &len_buf);
    EXPECT_EQ(S_ERR_BUFFER_LEN, error);
    EXPECT_EQ(73, n_read_comment);
    EXPECT_EQ(0, n_read_comment_reply);
    EXPECT_EQ(8176, len_buf);

    // free
    safe_free_b_list(&b_comments, n_comment);
    bson_safe_destroy(&fields);

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
