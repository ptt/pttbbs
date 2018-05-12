#include "gtest/gtest.h"
#include "test.h"
#include "cmpttdb/pttdb_comment_reply.h"
#include "cmpttdb/pttdb_comment_reply_private.h"

TEST(pttdb_comment_reply, create_comment_reply) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    UUID main_id = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char content[] = "temp_content\r\n";
    int len = strlen(content);
    int n_line = 1;

    UUID comment_id = {};
    UUID comment_reply_id = {};
    UUID tmp_comment_reply_id = {};

    gen_uuid(main_id, 0);

    // create comment
    Err error_code = create_comment(main_id, (char *)"poster0", (char *)"10.1.1.1", 10, (char *)"testtest\r\n", COMMENT_TYPE_GOOD, comment_id, 0);
    EXPECT_EQ(S_OK, error_code);

    error_code = create_comment_reply(main_id, comment_id, poster, ip, len, content, comment_reply_id, 0);
    EXPECT_EQ(S_OK, error_code);

    CommentReply comment_reply = {};
    init_comment_reply_buf(&comment_reply);

    memcpy(tmp_comment_reply_id, comment_reply_id, UUIDLEN);

    error_code = read_comment_reply(comment_reply_id, &comment_reply);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(0, strncmp((char *)tmp_comment_reply_id, (char *)comment_reply.the_id, UUIDLEN));

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

    EXPECT_EQ(len, comment_reply.len_total);
    EXPECT_EQ(n_line, comment_reply.n_total_line);
    EXPECT_EQ(1, comment_reply.n_block);

    Comment comment = {};
    init_comment_buf(&comment);
    error_code = read_comment(comment_id, &comment);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(0, strncmp((char *)comment.comment_reply_id, (char *)comment_reply.the_id, UUIDLEN));
    EXPECT_EQ(comment.n_comment_reply_line, comment_reply.n_line);

    destroy_comment_reply(&comment_reply);
    destroy_comment(&comment);
}

TEST(pttdb_comment_reply, create_comment_reply_n_only) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    UUID main_id = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char content[] = "temp_content\n";
    int len = strlen(content);
    int n_line = 1;

    UUID comment_id = {};
    UUID comment_reply_id = {};
    UUID tmp_comment_reply_id = {};

    gen_uuid(main_id, 0);

    // create comment
    Err error_code = create_comment(main_id, (char *)"poster0", (char *)"10.1.1.1", 9, (char *)"testtest\n", COMMENT_TYPE_GOOD, comment_id, 0);
    EXPECT_EQ(S_OK, error_code);

    error_code = create_comment_reply(main_id, comment_id, poster, ip, len, content, comment_reply_id, 0);
    EXPECT_EQ(S_OK, error_code);

    CommentReply comment_reply = {};
    init_comment_reply_buf(&comment_reply);

    memcpy(tmp_comment_reply_id, comment_reply_id, UUIDLEN);

    error_code = read_comment_reply(comment_reply_id, &comment_reply);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(0, strncmp((char *)tmp_comment_reply_id, (char *)comment_reply.the_id, UUIDLEN));

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

    Comment comment = {};
    init_comment_buf(&comment);
    error_code = read_comment(comment_id, &comment);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(0, strncmp((char *)comment.comment_reply_id, (char *)comment_reply.the_id, UUIDLEN));
    EXPECT_EQ(comment.n_comment_reply_line, comment_reply.n_line);

    destroy_comment_reply(&comment_reply);
    destroy_comment(&comment);
}

TEST(pttdb_comment_reply, create_comment_reply_extreme_long_comment) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    UUID main_id = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char content[MAX_BUF_SIZE * 2] = {};
    char *p_content = content;
    for(int i = 0; i < 1000; i++) {
        sprintf(p_content, "temp5678\r\n");
        p_content += 10;
    }
    int len = strlen(content);
    int n_line = 1000;


    UUID comment_id = {};
    UUID comment_reply_id = {};
    UUID tmp_comment_reply_id = {};

    gen_uuid(main_id, 0);

    // create comment
    Err error_code = create_comment(main_id, (char *)"poster0", (char *)"10.1.1.1", 10, (char *)"testtest\r\n", COMMENT_TYPE_GOOD, comment_id, 0);
    EXPECT_EQ(S_OK, error_code);

    error_code = create_comment_reply(main_id, comment_id, poster, ip, len, content, comment_reply_id, 0);
    EXPECT_EQ(S_OK, error_code);

    CommentReply comment_reply = {};
    init_comment_reply_buf(&comment_reply);

    memcpy(tmp_comment_reply_id, comment_reply_id, UUIDLEN);

    error_code = read_comment_reply(comment_reply_id, &comment_reply);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(0, strncmp((char *)tmp_comment_reply_id, (char *)comment_reply.the_id, UUIDLEN));

    EXPECT_EQ(0, strncmp((char *)main_id, (char *)comment_reply.main_id, UUIDLEN));
    EXPECT_EQ(LIVE_STATUS_ALIVE, comment_reply.status);

    EXPECT_STREQ(poster, comment_reply.status_updater);
    EXPECT_STREQ(ip, comment_reply.status_update_ip);

    EXPECT_STREQ(poster, comment_reply.poster);
    EXPECT_STREQ(ip, comment_reply.ip);

    EXPECT_STREQ(poster, comment_reply.updater);
    EXPECT_STREQ(ip, comment_reply.update_ip);

    EXPECT_EQ(comment_reply.create_milli_timestamp, comment_reply.update_milli_timestamp);

    EXPECT_EQ(2560, comment_reply.len);
    EXPECT_EQ(0, strncmp(content, comment_reply.buf, 2560));
    EXPECT_EQ(256, comment_reply.n_line);

    EXPECT_EQ(len, comment_reply.len_total);
    EXPECT_EQ(n_line, comment_reply.n_total_line);
    EXPECT_EQ(4, comment_reply.n_block);

    Comment comment = {};
    init_comment_buf(&comment);
    error_code = read_comment(comment_id, &comment);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(0, strncmp((char *)comment.comment_reply_id, (char *)comment_reply.the_id, UUIDLEN));
    EXPECT_EQ(comment.n_comment_reply_line, comment_reply.n_line);
    EXPECT_EQ(comment.n_comment_reply_total_line, comment_reply.n_total_line);

    destroy_comment_reply(&comment_reply);
    destroy_comment(&comment);
}

TEST(pttdb_comment_reply, create_comment_reply_extreme_long_comment_fd) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    UUID main_id = {};
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char content[MAX_BUF_SIZE * 2] = {};
    char *p_content = content;
    for(int i = 0; i < 1000; i++) {
        sprintf(p_content, "temp5678\r\n");
        p_content += 10;
    }
    int len = strlen(content);
    int n_line = 1000;

    FILE *f = fopen("data_test/test_create_comment_reply_extreme_long_comment_fd.txt", "w");
    fprintf(f, "%s", content);
    fclose(f);

    UUID comment_id = {};
    UUID comment_reply_id = {};
    UUID tmp_comment_reply_id = {};

    gen_uuid(main_id, 0);

    // create comment
    Err error_code = create_comment(main_id, (char *)"poster0", (char *)"10.1.1.1", 10, (char *)"testtest\r\n", COMMENT_TYPE_GOOD, comment_id, 0);
    EXPECT_EQ(S_OK, error_code);


    int fd = open("data_test/test_create_comment_reply_extreme_long_comment_fd.txt", O_RDONLY);

    error_code = create_comment_reply_from_fd(main_id, comment_id, poster, ip, len, fd, comment_reply_id, 0);
    EXPECT_EQ(S_OK, error_code);

    close(fd);

    CommentReply comment_reply = {};
    init_comment_reply_buf(&comment_reply);

    memcpy(tmp_comment_reply_id, comment_reply_id, UUIDLEN);

    error_code = read_comment_reply(comment_reply_id, &comment_reply);
    EXPECT_EQ(S_OK, error_code);

    EXPECT_EQ(0, strncmp((char *)tmp_comment_reply_id, (char *)comment_reply.the_id, UUIDLEN));

    EXPECT_EQ(0, strncmp((char *)main_id, (char *)comment_reply.main_id, UUIDLEN));
    EXPECT_EQ(LIVE_STATUS_ALIVE, comment_reply.status);

    EXPECT_STREQ(poster, comment_reply.status_updater);
    EXPECT_STREQ(ip, comment_reply.status_update_ip);

    EXPECT_STREQ(poster, comment_reply.poster);
    EXPECT_STREQ(ip, comment_reply.ip);

    EXPECT_STREQ(poster, comment_reply.updater);
    EXPECT_STREQ(ip, comment_reply.update_ip);

    EXPECT_EQ(comment_reply.create_milli_timestamp, comment_reply.update_milli_timestamp);

    EXPECT_EQ(2560, comment_reply.len);
    EXPECT_EQ(0, strncmp(content, comment_reply.buf, 2560));
    EXPECT_EQ(256, comment_reply.n_line);

    EXPECT_EQ(len, comment_reply.len_total);
    EXPECT_EQ(n_line, comment_reply.n_total_line);
    EXPECT_EQ(4, comment_reply.n_block);

    Comment comment = {};
    init_comment_buf(&comment);
    error_code = read_comment(comment_id, &comment);
    EXPECT_EQ(S_OK, error_code);
    EXPECT_EQ(0, strncmp((char *)comment.comment_reply_id, (char *)comment_reply.the_id, UUIDLEN));
    EXPECT_EQ(comment.n_comment_reply_line, comment_reply.n_line);
    EXPECT_EQ(comment.n_comment_reply_total_line, comment_reply.n_total_line);

    destroy_comment_reply(&comment_reply);
    destroy_comment(&comment);
}

TEST(pttdb_comment_reply, delete_comment_reply) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    UUID main_id;
    char poster[IDLEN + 1] = {};
    char ip[IPV4LEN + 1] = {};
    char content[] = "temp_content\n";
    int len = strlen(content);

    UUID comment_reply_id = {};
    UUID comment_id = {};    

    gen_uuid(main_id, 0);
    gen_uuid(comment_id, 0);

    Err error = create_comment_reply(main_id, comment_id, poster, ip, len, content, comment_reply_id, 0);
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

    gen_uuid(main_id, 0);
    gen_uuid(comment_id, 0);
    gen_uuid(comment_id2, 0);

    Err error = S_OK;
    error = create_comment_reply(main_id, comment_id, (char *)"poster1", (char *)"10.3.1.4", 24, (char *)"test1test1\r\ntest3test3\r\n", comment_reply_id, 0);
    EXPECT_EQ(S_OK, error);
    error = create_comment_reply(main_id, comment_id2, (char *)"poster1", (char *)"10.3.1.4", 12, (char *)"test2test2\r\n", comment_reply_id2, 0);
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

TEST(pttdb_comment_reply, get_comment_reply_info_by_main_n_only) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    UUID main_id = {};
    UUID comment_reply_id = {};
    UUID comment_reply_id2 = {};
    UUID comment_id = {};
    UUID comment_id2 = {};

    gen_uuid(main_id, 0);
    gen_uuid(comment_id, 0);
    gen_uuid(comment_id2, 0);

    Err error = S_OK;
    error = create_comment_reply(main_id, comment_id, (char *)"poster1", (char *)"10.3.1.4", 22, (char *)"test1test1\ntest3test3\n", comment_reply_id, 0);
    EXPECT_EQ(S_OK, error);
    error = create_comment_reply(main_id, comment_id2, (char *)"poster1", (char *)"10.3.1.4", 11, (char *)"test2test2\n", comment_reply_id2, 0);
    EXPECT_EQ(S_OK, error);

    int n_total_comments = 0;
    int total_len = 0;
    int n_line = 0;
    error = get_comment_reply_info_by_main(main_id, &n_total_comments, &n_line, &total_len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2, n_total_comments);
    EXPECT_EQ(3, n_line);
    EXPECT_EQ(33, total_len);
}

TEST(pttdb_comment_reply, serialize_comment_reply_bson) {
    CommentReply comment_reply = {};
    CommentReply comment_reply2 = {};

    init_comment_reply_buf(&comment_reply);
    init_comment_reply_buf(&comment_reply2);

    comment_reply.version = 2;
    gen_uuid(comment_reply.the_id, 0);
    memcpy(comment_reply.comment_id, comment_reply.the_id, UUIDLEN);
    gen_uuid(comment_reply.main_id, 0);
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

    Err error = serialize_comment_reply_bson(&comment_reply, &comment_reply_bson);
    EXPECT_EQ(S_OK, error);

    char *str = bson_as_canonical_extended_json(comment_reply_bson, NULL);
    fprintf(stderr, "test_pttdb_comment_reply.serialize_comment_reply_bson: comment_reply_bson: %s\n", str);
    bson_free(str);

    error = deserialize_comment_reply_bson(comment_reply_bson, &comment_reply2);

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

TEST(pttdb_comment_reply, read_comment_replys_by_query_to_bsons) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    UUID comment_reply_id = {};
    gen_uuid(main_id, 0);

    Comment comment = {};
    init_comment_buf(&comment);
    // comment and comment-reply
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

    char buf[MAX_BUF_SIZE * 2] = {};
    char *p_buf = NULL;

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

        p_buf = buf;
        for(int j = 0; j < i; j++) {
            sprintf(p_buf, "testtest901234567890223456789032345678904234567890523456789062345678907234567890823456789092345678\r\n");
            p_buf += 100;
        }

        error = create_comment_reply(main_id, comment_id, (char *)"reply001", (char *)"10.1.1.5", i * 100, buf, comment_reply_id, 0);
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

        p_buf = buf;
        for(int j = 0; j < i; j++) {
            sprintf(p_buf, "testtest\r\n");
            p_buf += 10;
        }

        error = create_comment_reply(main_id, comment_id, (char *)"reply001", (char *)"10.1.1.5", i * 10, buf, comment_reply_id, 0);
        EXPECT_EQ(S_OK, error);
    }

    int the_i = 0;
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

        the_i = i ? i : 1;

        p_buf = buf;
        for(int j = 0; j < the_i; j++) {
            sprintf(p_buf, "testtest\r\n");
            p_buf += 10;
        }

        create_comment_reply(main_id, comment_id, (char *)"reply001", (char *)"10.1.1.5", the_i * 10, buf, comment_reply_id, 0);
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

    // read comment-replys
    // construct query
    bson_t *q_array = bson_new();
    bson_t child;
    bzero(buf, MAX_BUF_SIZE);
    const char *key;
    size_t keylen;
    bool status;
    BSON_APPEND_ARRAY_BEGIN(q_array, "$in", &child);
    bson_t **p_b_comments = b_comments;
    for(int i = 0; i < 100; i++, p_b_comments++) {
        keylen = bson_uint32_to_string(i, &key, buf, sizeof(buf));
        error = bson_get_value_bin(*p_b_comments, (char *)"the_id", UUIDLEN, (char *)comment_id, &len);
        status = bson_append_bin(&child, key, (int)keylen, comment_id, UUIDLEN);
        if (!status) {
            error = S_ERR_INIT;
            break;
        }
    }
    bson_append_array_end(q_array, &child);

    bson_t *query = BCON_NEW(
        "comment_id", BCON_DOCUMENT(q_array)
        );

    bson_safe_destroy(&fields);
    fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "comment_id", BCON_BOOL(true),
        "the_id", BCON_BOOL(true),
        "n_line", BCON_BOOL(true)
        );

    bson_t **b_comment_replys = (bson_t **)malloc(sizeof(bson_t *) * n_comment);

    int n_comment_reply = 0;
    error = read_comment_replys_by_query_to_bsons(query, fields, n_comment, b_comment_replys, &n_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment_reply);

    // free
    safe_free_b_list(&b_comment_replys, n_comment_reply);

    bson_safe_destroy(&query);
    bson_safe_destroy(&q_array);

    safe_free_b_list(&b_comments, n_comment);

    bson_safe_destroy(&fields);

    destroy_comment(&comment);
}

TEST(pttdb_comment_reply, read_comment_replys_by_query_to_bsons_n_only) {
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    Err error = S_OK;
    UUID main_id = {};
    UUID comment_id = {};
    UUID comment_reply_id = {};
    gen_uuid(main_id, 0);

    Comment comment = {};
    init_comment_buf(&comment);
    // comment and comment-reply
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

    strcpy(comment.buf, "test1test1\n");
    comment.len = 9;

    bson_t *comment_id_bson = NULL;
    bson_t *comment_bson = NULL;

    int n_comment = 100;

    char buf[MAX_BUF_SIZE] = {};
    char *p_buf = NULL;

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

        p_buf = buf;
        for(int j = 0; j < i; j++) {
            sprintf(p_buf, "testtest\n");
            p_buf += 9;
        }

        error = create_comment_reply(main_id, comment_id, (char *)"reply001", (char *)"10.1.1.5", i * 9, buf, comment_reply_id, 0);
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

        p_buf = buf;
        for(int j = 0; j < i; j++) {
            sprintf(p_buf, "testtest\n");
            p_buf += 9;
        }

        error = create_comment_reply(main_id, comment_id, (char *)"reply001", (char *)"10.1.1.5", i * 9, buf, comment_reply_id, 0);
        EXPECT_EQ(S_OK, error);
    }

    int the_i = 0;
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

        the_i = i ? i : 1;

        p_buf = buf;
        for(int j = 0; j < the_i; j++) {
            sprintf(p_buf, "testtest\n");
            p_buf += 9;
        }

        create_comment_reply(main_id, comment_id, (char *)"reply001", (char *)"10.1.1.5", the_i * 9, buf, comment_reply_id, 0);
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

    // read comment-replys
    // construct query
    bson_t *q_array = bson_new();
    bson_t child;
    bzero(buf, MAX_BUF_SIZE);
    const char *key;
    size_t keylen;
    bool status;
    BSON_APPEND_ARRAY_BEGIN(q_array, "$in", &child);
    bson_t **p_b_comments = b_comments;
    for(int i = 0; i < 100; i++, p_b_comments++) {
        keylen = bson_uint32_to_string(i, &key, buf, sizeof(buf));
        error = bson_get_value_bin(*p_b_comments, (char *)"the_id", UUIDLEN, (char *)comment_id, &len);
        status = bson_append_bin(&child, key, (int)keylen, comment_id, UUIDLEN);
        if (!status) {
            error = S_ERR_INIT;
            break;
        }
    }
    bson_append_array_end(q_array, &child);

    bson_t *query = BCON_NEW(
        "comment_id", BCON_DOCUMENT(q_array)
        );

    bson_safe_destroy(&fields);
    fields = BCON_NEW(
        "_id", BCON_BOOL(false),
        "comment_id", BCON_BOOL(true),
        "the_id", BCON_BOOL(true),
        "n_line", BCON_BOOL(true)
        );

    bson_t **b_comment_replys = (bson_t **)malloc(sizeof(bson_t *) * n_comment);

    int n_comment_reply = 0;
    error = read_comment_replys_by_query_to_bsons(query, fields, n_comment, b_comment_replys, &n_comment_reply);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_comment_reply);

    // free
    safe_free_b_list(&b_comment_replys, n_comment_reply);

    bson_safe_destroy(&query);
    bson_safe_destroy(&q_array);

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
