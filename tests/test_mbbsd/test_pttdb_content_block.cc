#include "gtest/gtest.h"
#include "bbs.h"
#include "ptterr.h"
#include "pttdb.h"
#include "pttdb_internal.h"
#include "util_db_internal.h"

TEST(pttdb, save_content_block) {
    Err error;

    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    UUID ref_id;
    UUID content_id;
    gen_uuid(ref_id);
    gen_uuid(content_id);

    ContentBlock content_block = {};
    ContentBlock content_block2 = {};

    // init content-block
    error = reset_content_block(&content_block, ref_id, content_id, 3);
    EXPECT_EQ(S_OK, error);

    char buf[] = "test_buf\r\ntest2";
    int len_buf = strlen(buf);
    associate_content_block(&content_block, buf, len_buf);
    content_block.len_block = len_buf;
    content_block.n_line = 1;

    // init content-block2
    error = init_content_block_buf_block(&content_block2);
    EXPECT_EQ(S_OK, error);

    // save
    error = save_content_block(&content_block, MONGO_MAIN_CONTENT);
    EXPECT_EQ(S_OK, error);

    error = read_content_block(content_id, 3, MONGO_MAIN_CONTENT, &content_block2);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, strncmp(content_block2.buf_block, buf, len_buf));
    EXPECT_EQ(MAX_BUF_SIZE, content_block2.max_buf_len);
    EXPECT_EQ(len_buf, content_block2.len_block);
    EXPECT_EQ(1, content_block2.n_line);

    dissociate_content_block(&content_block);
    destroy_content_block(&content_block2);
}

TEST(pttdb, read_content_block_forgot_init) {
    Err error;

    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    UUID ref_id;
    UUID content_id;
    gen_uuid(ref_id);
    gen_uuid(content_id);

    ContentBlock content_block = {};
    ContentBlock content_block2 = {};

    // init content-block
    error = reset_content_block(&content_block, ref_id, content_id, 3);
    EXPECT_EQ(S_OK, error);

    char buf[] = "test_buf\r\ntest2";
    int len_buf = strlen(buf);
    associate_content_block(&content_block, buf, len_buf);
    content_block.len_block = len_buf;
    content_block.n_line = 1;

    // init content-block2
    // error = init_content_block_buf_block(&content_block2);
    // EXPECT_EQ(S_OK, error);

    // save
    error = save_content_block(&content_block, MONGO_MAIN_CONTENT);
    EXPECT_EQ(S_OK, error);

    error = read_content_block(content_id, 3, MONGO_MAIN_CONTENT, &content_block2);
    EXPECT_EQ(S_ERR, error);

    dissociate_content_block(&content_block);
    //destroy_content_block(&content_block2);
}

TEST(pttdb, reset_content_block) {
    Err error;
    ContentBlock content_block = {};

    UUID ref_id;
    UUID content_id;
    gen_uuid(ref_id);
    gen_uuid(content_id);

    error = reset_content_block(&content_block, ref_id, content_id, 3);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(NULL, content_block.buf_block);
    EXPECT_EQ(0, content_block.max_buf_len);
    EXPECT_EQ(0, strncmp((char *)ref_id, (char *)content_block.ref_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_id, (char *)content_block.the_id, UUIDLEN));
    EXPECT_EQ(3, content_block.block_id);

    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(NULL, content_block.buf_block);
    EXPECT_EQ(0, content_block.max_buf_len);
}

TEST(pttdb, init_content_block_with_buf_block) {
    Err error;
    ContentBlock content_block = {};

    UUID ref_id;
    UUID content_id;
    gen_uuid(ref_id);
    gen_uuid(content_id);

    error = init_content_block_with_buf_block(&content_block, ref_id, content_id, 3);
    EXPECT_EQ(S_OK, error);
    EXPECT_NE(NULL, (unsigned long)content_block.buf_block);
    EXPECT_EQ(MAX_BUF_SIZE, content_block.max_buf_len);
    EXPECT_EQ(0, strncmp((char *)ref_id, (char *)content_block.ref_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_id, (char *)content_block.the_id, UUIDLEN));
    EXPECT_EQ(3, content_block.block_id);

    error = destroy_content_block(&content_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(NULL, content_block.buf_block);
    EXPECT_EQ(0, content_block.max_buf_len);
}

TEST(pttdb, associate_content_block) {
    Err error;
    ContentBlock content_block = {};

    UUID ref_id;
    UUID content_id;
    gen_uuid(ref_id);
    gen_uuid(content_id);

    error = reset_content_block(&content_block, ref_id, content_id, 3);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(NULL, content_block.buf_block);
    EXPECT_EQ(0, content_block.max_buf_len);
    EXPECT_EQ(0, strncmp((char *)ref_id, (char *)content_block.ref_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_id, (char *)content_block.the_id, UUIDLEN));
    EXPECT_EQ(3, content_block.block_id);

    char buf[20];
    error = associate_content_block(&content_block, buf, 20);
    EXPECT_EQ(buf, content_block.buf_block);
    EXPECT_EQ(20, content_block.max_buf_len);

    error = dissociate_content_block(&content_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(NULL, content_block.buf_block);
    EXPECT_EQ(0, content_block.max_buf_len);
}

TEST(pttdb, serialize_content_block_bson) {
    ContentBlock content_block = {};
    ContentBlock content_block2 = {};

    init_content_block_buf_block(&content_block);
    init_content_block_buf_block(&content_block2);

    // initialize
    gen_uuid(content_block.the_id);
    gen_uuid(content_block.ref_id);
    content_block.block_id = 53;
    content_block.n_line = 1;
    char str[] = "test123\r\n";
    content_block.len_block = strlen(str);
    memcpy(content_block.buf_block, str, strlen(str));

    // init-op
    bson_t *b = NULL;

    // do-op
    Err error = _serialize_content_block_bson(&content_block, &b);
    EXPECT_EQ(S_OK, error);

    error = _deserialize_content_block_bson(b, &content_block2);
    EXPECT_EQ(S_OK, error);

    // post-op
    bson_safe_destroy(&b);

    // expect
    EXPECT_EQ(0, strncmp((char *)content_block.the_id, (char *)content_block2.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_block.ref_id, (char *)content_block2.ref_id, UUIDLEN));
    EXPECT_EQ(content_block.block_id, content_block2.block_id);
    EXPECT_EQ(content_block.len_block, content_block2.len_block);
    EXPECT_EQ(content_block.n_line, content_block2.n_line);
    EXPECT_STREQ(content_block.buf_block, content_block2.buf_block);

    destroy_content_block(&content_block);
    destroy_content_block(&content_block2);
}

TEST(pttdb, dynamic_read_content_blocks)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    bson_t *b[10];
    UUID content_id;
    UUID ref_id;

    gen_uuid(content_id);
    gen_uuid(ref_id);
    for (int i = 0; i < 10; i++) {
        b[i] = BCON_NEW(
                   "the_id", BCON_BINARY(content_id, UUIDLEN),
                   "block_id", BCON_INT32(i),
                   "ref_id", BCON_BINARY(ref_id, UUIDLEN),
                   "len_block", BCON_INT32(5),
                   "n_line", BCON_INT32(0),
                   "buf_block", BCON_BINARY((unsigned char *)"test1", 5)
               );
        db_update_one(MONGO_MAIN_CONTENT, b[i], b[i], true);
    }

    // content-block initialize
    ContentBlock content_blocks[10] = {};

    int len;
    int n_block;
    char buf[51] = {};
    Err error = dynamic_read_content_blocks(content_id, 10, 0, MONGO_MAIN_CONTENT, buf, 50, content_blocks, &n_block, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_block);
    EXPECT_EQ(50, len);
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(0, strncmp((char *)content_id, (char *)content_blocks[i].the_id, UUIDLEN));
        EXPECT_EQ(0, strncmp((char *)ref_id, (char *)content_blocks[i].ref_id, UUIDLEN));
        EXPECT_EQ(5, content_blocks[i].len_block);
        EXPECT_EQ(0, content_blocks[i].n_line);
        EXPECT_EQ(0, strncmp((char *)"test1", content_blocks[i].buf_block, 5));
        EXPECT_EQ(i, content_blocks[i].block_id);
    }

    EXPECT_STREQ("test1test1test1test1test1test1test1test1test1test1", buf);

    for (int i = 0; i < 10; i++) {
        dissociate_content_block(&content_blocks[i]);
        bson_safe_destroy(&b[i]);
    }
}

TEST(pttdb, dynamic_read_content_blocks2)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    bson_t *b[10];
    UUID content_id;
    UUID ref_id;

    gen_uuid(content_id);
    gen_uuid(ref_id);
    for (int i = 0; i < 10; i++) {
        b[i] = BCON_NEW(
                   "the_id", BCON_BINARY(content_id, UUIDLEN),
                   "block_id", BCON_INT32(i),
                   "ref_id", BCON_BINARY(ref_id, UUIDLEN),
                   "len_block", BCON_INT32(5),
                   "n_line", BCON_INT32(0),
                   "buf_block", BCON_BINARY((unsigned char *)"test1", 5)
               );
        db_update_one(MONGO_MAIN_CONTENT, b[i], b[i], true);
    }

    // content-block initialize
    ContentBlock content_blocks[10] = {};

    int len;
    int n_block;
    char buf[41] = {};
    Err error = dynamic_read_content_blocks(content_id, 10, 0, MONGO_MAIN_CONTENT, buf, 40, content_blocks, &n_block, &len);
    EXPECT_EQ(S_ERR_BUFFER_LEN, error);
    EXPECT_EQ(8, n_block);
    EXPECT_EQ(40, len);
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(0, strncmp((char *)content_id, (char *)content_blocks[i].the_id, UUIDLEN));
        EXPECT_EQ(0, strncmp((char *)ref_id, (char *)content_blocks[i].ref_id, UUIDLEN));
        EXPECT_EQ(5, content_blocks[i].len_block);
        EXPECT_EQ(0, content_blocks[i].n_line);
        EXPECT_EQ(0, strncmp((char *)"test1", content_blocks[i].buf_block, 5));
        EXPECT_EQ(i, content_blocks[i].block_id);
    }

    EXPECT_STREQ("test1test1test1test1test1test1test1test1", buf);

    for (int i = 0; i < 10; i++) {
        dissociate_content_block(&content_blocks[i]);
        bson_safe_destroy(&b[i]);
    }
}

TEST(pttdb, read_content_blocks)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    bson_t *b[10];
    UUID content_id;
    UUID ref_id;

    gen_uuid(content_id);
    gen_uuid(ref_id);
    for (int i = 0; i < 10; i++) {
        b[i] = BCON_NEW(
                   "the_id", BCON_BINARY(content_id, UUIDLEN),
                   "block_id", BCON_INT32(i),
                   "ref_id", BCON_BINARY(ref_id, UUIDLEN),
                   "len_block", BCON_INT32(5),
                   "n_line", BCON_INT32(0),
                   "buf_block", BCON_BINARY((unsigned char *)"test1", 5)
               );
        db_update_one(MONGO_MAIN_CONTENT, b[i], b[i], true);
    }

    // content-block initialize
    ContentBlock content_blocks[10] = {};
    for (int i = 0; i < 10; i++) {
        init_content_block_buf_block(&content_blocks[i]);
    }

    int len;
    int n_block;
    Err error = read_content_blocks(content_id, 10, 0, MONGO_MAIN_CONTENT, content_blocks, &n_block, &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_block);
    EXPECT_EQ(50, len);
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(0, strncmp((char *)content_id, (char *)content_blocks[i].the_id, UUIDLEN));
        EXPECT_EQ(0, strncmp((char *)ref_id, (char *)content_blocks[i].ref_id, UUIDLEN));
        EXPECT_EQ(5, content_blocks[i].len_block);
        EXPECT_EQ(0, content_blocks[i].n_line);
        EXPECT_EQ(0, strncmp((char *)"test1", content_blocks[i].buf_block, 5));
        EXPECT_EQ(i, content_blocks[i].block_id);
    }

    for (int i = 0; i < 10; i++) {
        destroy_content_block(&content_blocks[i]);
        bson_safe_destroy(&b[i]);
    }
}

TEST(pttdb, read_content_blocks_get_db_results)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    bson_t *b[10];
    UUID content_id;
    UUID ref_id;

    gen_uuid(content_id);
    gen_uuid(ref_id);
    for (int i = 0; i < 10; i++) {
        b[i] = BCON_NEW(
                   "the_id", BCON_BINARY(content_id, UUIDLEN),
                   "block_id", BCON_INT32(i),
                   "ref_id", BCON_BINARY(ref_id, UUIDLEN),
                   "len_block", BCON_INT32(5),
                   "n_line", BCON_INT32(0),
                   "buf_block", BCON_BINARY((unsigned char *)"test1", 5)
               );
        db_update_one(MONGO_MAIN_CONTENT, b[i], b[i], true);
    }

    bson_t *b2[10] = {};
    int n_block;
    bson_t *key = BCON_NEW(
            "the_id", BCON_BINARY(content_id, UUIDLEN)
        );
    Err error = _read_content_blocks_get_db_results(b2, key, 10, 0, MONGO_MAIN_CONTENT, &n_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(10, n_block);

    error = _ensure_block_ids(b2, 0, 10);
    EXPECT_EQ(S_OK, error);

    bson_safe_destroy(&key);
    for (int i = 0; i < 10; i++) {
        bson_safe_destroy(&b[i]);
        bson_safe_destroy(&b2[i]);
    }
}

TEST(pttdb, form_b_array_block_ids)
{
    bson_t *b = bson_new();

    Err error = _form_b_array_block_ids(5, 10, b);
    EXPECT_EQ(S_OK, error);

    char *str = bson_as_canonical_extended_json(b, NULL);
    EXPECT_STREQ("{ \"$in\" : [ { \"$numberInt\" : \"5\" }, { \"$numberInt\" : \"6\" }, { \"$numberInt\" : \"7\" }, { \"$numberInt\" : \"8\" }, { \"$numberInt\" : \"9\" }, { \"$numberInt\" : \"10\" }, { \"$numberInt\" : \"11\" }, { \"$numberInt\" : \"12\" }, { \"$numberInt\" : \"13\" }, { \"$numberInt\" : \"14\" } ] }", str);
    fprintf(stderr, "test_pttdb_content_block.form_b_array_block_ids: str: %s\n", str);
    free(str);

    bson_safe_destroy(&b);
}

TEST(pttdb, ensure_block_ids)
{
    bson_t *b[10];
    for (int i = 0; i < 10; i++) {
        b[i] = BCON_NEW("block_id", BCON_INT32(i));
    }

    Err error = _ensure_block_ids(b, 0, 10);
    EXPECT_EQ(S_OK, error);

    error = _ensure_block_ids(b, 1, 10);
    EXPECT_EQ(S_ERR, error);

    for (int i = 0; i < 10; i++) {
        bson_safe_destroy(&b[i]);
    }
}

TEST(pttdb, ensure_block_ids2)
{
    bson_t *b[10];
    for (int i = 0; i < 10; i++) {
        b[i] = BCON_NEW("block_id", BCON_INT32(i + 5));
    }

    Err error = _ensure_block_ids(b, 5, 10);
    EXPECT_EQ(S_OK, error);

    error = _ensure_block_ids(b, 1, 10);
    EXPECT_EQ(S_ERR, error);

    for (int i = 0; i < 10; i++) {
        bson_safe_destroy(&b[i]);
    }
}

TEST(pttdb, sort_by_block_id)
{
    bson_t *b[10];
    for (int i = 0; i < 10; i++) {
        b[i] = BCON_NEW("block_id", BCON_INT32(9 - i));
    }
    Err error = _sort_by_block_id(b, 10);
    EXPECT_EQ(S_OK, error);

    error = _ensure_block_ids(b, 0, 10);
    EXPECT_EQ(S_OK, error);

    for (int i = 0; i < 10; i++) {
        bson_safe_destroy(&b[i]);
    }
}

TEST(pttdb, split_contents_core)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    ContentBlock content_block = {};
    char buf[MAX_BUF_SIZE * 2] = {};
    char *p_buf = buf;
    for (int i = 0; i < 1000; i++) {
        sprintf(p_buf, "test456789\r\n");
        p_buf += 12;
    }
    int bytes = strlen(buf);

    UUID ref_id;
    UUID content_id;
    gen_uuid(ref_id);
    gen_uuid(content_id);

    int n_block = 0;
    init_content_block_with_buf_block(&content_block, ref_id, content_id, n_block);
    n_block++;

    int n_line = 0;
    char line[MAX_BUF_SIZE];
    int bytes_in_line = 0;
    Err error = _split_contents_core(buf, bytes, ref_id, content_id, MONGO_MAIN_CONTENT, &n_line, &n_block, line, &bytes_in_line, &content_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1000, n_line);
    EXPECT_EQ(4, n_block);
    EXPECT_EQ(0, bytes_in_line);
    EXPECT_EQ(232, content_block.n_line);
    EXPECT_STREQ(buf + 9216, content_block.buf_block);
    EXPECT_EQ(2784, content_block.len_block);

    destroy_content_block(&content_block);
}

TEST(pttdb, split_contents_core2)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    ContentBlock content_block = {};
    char buf[MAX_BUF_SIZE * 2] = {};
    char *p_buf = buf;
    for (int i = 0; i < 100; i++) {
        sprintf(p_buf, "test456789test456789test456789test456789test456789test456789test456789test456789test456789test456789\r\n");
        p_buf += 102;
    }
    int bytes = strlen(buf);

    UUID ref_id;
    UUID content_id;
    gen_uuid(ref_id);
    gen_uuid(content_id);

    int n_block = 0;
    init_content_block_with_buf_block(&content_block, ref_id, content_id, n_block);
    n_block++;

    int n_line = 0;
    char line[MAX_BUF_SIZE];
    int bytes_in_line = 0;
    Err error = _split_contents_core(buf, bytes, ref_id, content_id, MONGO_MAIN_CONTENT, &n_line, &n_block, line, &bytes_in_line, &content_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(100, n_line);
    EXPECT_EQ(2, n_block);
    EXPECT_EQ(0, bytes_in_line);
    EXPECT_EQ(20, content_block.n_line);
    EXPECT_STREQ(buf + 8160, content_block.buf_block);
    EXPECT_EQ(2040, content_block.len_block);

    destroy_content_block(&content_block);
}

TEST(pttdb, split_contents_core3)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    ContentBlock content_block = {};
    char buf[MAX_BUF_SIZE * 4] = {};
    char *p_buf = buf;
    sprintf(p_buf, "test456789test456789test456789test456789test456789test456789test456789test456789test456789test456789\r\n");
    p_buf += 102;

    for (int i = 0; i < 200; i++) {
        sprintf(p_buf, "test456789test456789test456789test456789test456789test456789test456789test456789test456789test456789");
        p_buf += 100;
    }
    sprintf(p_buf, "\r\n");
    int bytes = strlen(buf);

    UUID ref_id;
    UUID content_id;
    gen_uuid(ref_id);
    gen_uuid(content_id);

    int n_block = 0;
    init_content_block_with_buf_block(&content_block, ref_id, content_id, n_block);
    n_block++;

    int n_line = 0;
    char line[MAX_BUF_SIZE];
    int bytes_in_line = 0;
    Err error = _split_contents_core(buf, bytes, ref_id, content_id, MONGO_MAIN_CONTENT, &n_line, &n_block, line, &bytes_in_line, &content_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2, n_line);
    EXPECT_EQ(4, n_block);

    // the rest 3618-block in the current block.
    EXPECT_EQ(0, bytes_in_line);
    EXPECT_EQ(1, content_block.n_line);
    EXPECT_STREQ(buf + 102 + 8192 * 2, content_block.buf_block);
    EXPECT_EQ(3618, content_block.len_block);
    EXPECT_EQ(3, content_block.block_id);

    ContentBlock content_block2 = {};
    init_content_block_buf_block(&content_block2);

    // only the 1st line get into the block-0.
    error = read_content_block(content_id, 0, MONGO_MAIN_CONTENT, &content_block2);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(102, content_block2.len_block);
    EXPECT_EQ(0, strncmp(buf, content_block2.buf_block, 102));
    EXPECT_EQ(1, content_block2.n_line);
    error = reset_content_block_buf_block(&content_block2);
    EXPECT_EQ(S_OK, error);

    // the 1st 8192-block in the block-1
    error = read_content_block(content_id, 1, MONGO_MAIN_CONTENT, &content_block2);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(8192, content_block2.len_block);
    EXPECT_EQ(0, strncmp(buf + 102, content_block2.buf_block, 8192));
    EXPECT_EQ(0, content_block2.n_line);
    error = reset_content_block_buf_block(&content_block2);
    EXPECT_EQ(S_OK, error);

    // the 2nd 8192-block in the block-2
    error = read_content_block(content_id, 2, MONGO_MAIN_CONTENT, &content_block2);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(8192, content_block2.len_block);
    EXPECT_EQ(0, strncmp(buf + 102 + 8192, content_block2.buf_block, 8192));
    EXPECT_EQ(0, content_block2.n_line);
    error = reset_content_block_buf_block(&content_block2);
    EXPECT_EQ(S_OK, error);

    destroy_content_block(&content_block2);
    destroy_content_block(&content_block);
}

TEST(pttdb, split_contents_core4)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    ContentBlock content_block = {};
    char buf[MAX_BUF_SIZE * 4] = {};
    char *p_buf = buf;
    sprintf(p_buf, "test456789test456789test456789test456789test456789test456789test456789test456789test456789test456789\r\n");
    p_buf += 102;

    for (int i = 0; i < 200; i++) {
        sprintf(p_buf, "test456789test456789test456789test456789test456789test456789test456789test456789test456789test456789");
        p_buf += 100;
    }
    int bytes = strlen(buf);

    UUID ref_id;
    UUID content_id;
    gen_uuid(ref_id);
    gen_uuid(content_id);

    int n_block = 0;
    init_content_block_with_buf_block(&content_block, ref_id, content_id, n_block);
    n_block++;

    int n_line = 0;
    char line[MAX_BUF_SIZE];
    int bytes_in_line = 0;
    Err error = _split_contents_core(buf, bytes, ref_id, content_id, MONGO_MAIN_CONTENT, &n_line, &n_block, line, &bytes_in_line, &content_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1, n_line);
    EXPECT_EQ(3, n_block);

    // the rest 3618-block in the line.
    EXPECT_EQ(3616, bytes_in_line);
    EXPECT_STREQ(buf + 102 + 8192 * 2, line);

    // the 2nd 8192-block in the content_block.
    EXPECT_EQ(0, content_block.n_line);
    EXPECT_EQ(8192, content_block.len_block);
    EXPECT_EQ(2, content_block.block_id);
    EXPECT_EQ(0, strncmp(buf + 102 + 8192, content_block.buf_block, content_block.len_block));

    ContentBlock content_block2 = {};
    init_content_block_buf_block(&content_block2);

    // only the 1st line get into the block-0.
    error = read_content_block(content_id, 0, MONGO_MAIN_CONTENT, &content_block2);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(102, content_block2.len_block);
    EXPECT_EQ(0, strncmp(buf, content_block2.buf_block, 102));
    EXPECT_EQ(1, content_block2.n_line);
    error = reset_content_block_buf_block(&content_block2);
    EXPECT_EQ(S_OK, error);

    // the 1st 8192-block in the block-1
    error = read_content_block(content_id, 1, MONGO_MAIN_CONTENT, &content_block2);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(8192, content_block2.len_block);
    EXPECT_EQ(0, strncmp(buf + 102, content_block2.buf_block, 8192));
    EXPECT_EQ(0, content_block2.n_line);
    error = reset_content_block_buf_block(&content_block2);
    EXPECT_EQ(S_OK, error);

    // the 2nd 8192-block not in the db yet.
    error = read_content_block(content_id, 2, MONGO_MAIN_CONTENT, &content_block2);
    EXPECT_EQ(S_ERR_NOT_EXISTS, error);

    destroy_content_block(&content_block2);
    destroy_content_block(&content_block);
}

TEST(pttdb, split_contents_core_one_line)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    ContentBlock content_block = {};
    char line[] = "testtesttesttest\r\n";
    int bytes_in_line = strlen(line);
    int n_line = 100;
    int n_block = 0;

    UUID ref_id;
    UUID content_id;
    gen_uuid(ref_id);
    gen_uuid(content_id);

    init_content_block_with_buf_block(&content_block, ref_id, content_id, n_block);
    n_block++;

    Err error = _split_contents_core_one_line(line, bytes_in_line, ref_id, content_id, MONGO_MAIN_CONTENT, &content_block, &n_line, &n_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(1, n_block);
    EXPECT_EQ(101, n_line);

    EXPECT_EQ(0, strncmp((char *)ref_id, (char *)content_block.ref_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_id, (char *)content_block.the_id, UUIDLEN));
    EXPECT_EQ(0, content_block.block_id);
    EXPECT_EQ(bytes_in_line, content_block.len_block);
    EXPECT_STREQ(line, content_block.buf_block);
    EXPECT_EQ(1, content_block.n_line);
    EXPECT_EQ(MAX_BUF_SIZE, content_block.max_buf_len);

    destroy_content_block(&content_block);
}

TEST(pttdb, split_contents_core_one_line2_reaching_max_line)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    ContentBlock content_block = {};
    char line[] = "testtesttesttest\r\n";
    int bytes_in_line = strlen(line);
    int n_line = MAX_BUF_LINES;
    int n_block = 0;

    UUID ref_id;
    UUID content_id;
    gen_uuid(ref_id);
    gen_uuid(content_id);

    init_content_block_with_buf_block(&content_block, ref_id, content_id, n_block);
    n_block++;

    char origin_line[MAX_BUF_SIZE] = {};
    char *p_char = content_block.buf_block;
    for (int i = 0; i < MAX_BUF_LINES; i++) {
        sprintf(p_char, "test1\r\n");
        p_char += strlen(p_char);
    }
    content_block.len_block = strlen(content_block.buf_block);
    content_block.n_line = MAX_BUF_LINES;
    strcpy(origin_line, content_block.buf_block);

    Err error = _split_contents_core_one_line(line, bytes_in_line, ref_id, content_id, MONGO_MAIN_CONTENT, &content_block, &n_line, &n_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2, n_block);
    EXPECT_EQ(MAX_BUF_LINES + 1, n_line);

    EXPECT_EQ(0, strncmp((char *)ref_id, (char *)content_block.ref_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_id, (char *)content_block.the_id, UUIDLEN));
    EXPECT_EQ(1, content_block.block_id);
    EXPECT_EQ(bytes_in_line, content_block.len_block);
    EXPECT_STREQ(line, content_block.buf_block);
    EXPECT_EQ(1, content_block.n_line);
    EXPECT_EQ(MAX_BUF_SIZE, content_block.max_buf_len);

    ContentBlock content_block2 = {};
    init_content_block_buf_block(&content_block2);

    error = read_content_block(content_id, 0, MONGO_MAIN_CONTENT, &content_block2);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(0, strncmp((char *)ref_id, (char *)content_block2.ref_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_id, (char *)content_block2.the_id, UUIDLEN));
    EXPECT_EQ(0, content_block2.block_id);
    EXPECT_EQ(strlen(origin_line), content_block2.len_block);
    EXPECT_STREQ(origin_line, content_block2.buf_block);

    destroy_content_block(&content_block2);
    destroy_content_block(&content_block);
}

TEST(pttdb, split_contents_deal_with_last_line_block)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);

    ContentBlock content_block = {};
    UUID the_id;
    UUID ref_id;
    gen_uuid(the_id);
    gen_uuid(ref_id);
    init_content_block_with_buf_block(&content_block, ref_id, the_id, 0);
    EXPECT_EQ(MAX_BUF_SIZE, content_block.max_buf_len);

    int bytes_in_line = 10;
    char line[] = "test1test1";
    int n_line;
    int n_block = 1;

    Err error = _split_contents_deal_with_last_line_block(bytes_in_line, line, ref_id, the_id, MONGO_MAIN_CONTENT, &content_block, &n_line, &n_block);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(0, n_line);
    EXPECT_EQ(1, n_block);
    EXPECT_STREQ("test1test1", content_block.buf_block);
    EXPECT_EQ(0, content_block.n_line);
    EXPECT_EQ(10, content_block.len_block);
    EXPECT_EQ(0, strncmp((char *)the_id, (char *)content_block.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)ref_id, (char *)content_block.ref_id, UUIDLEN));
    EXPECT_EQ(0, content_block.block_id);

    ContentBlock content_block2 = {};
    init_content_block_buf_block(&content_block2);
    EXPECT_EQ(MAX_BUF_SIZE, content_block2.max_buf_len);

    error = read_content_block(the_id, 0, MONGO_MAIN_CONTENT, &content_block2);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(0, strncmp((char *)content_block2.the_id, (char *)content_block.the_id, UUIDLEN));
    EXPECT_EQ(0, strncmp((char *)content_block2.ref_id, (char *)content_block.ref_id, UUIDLEN));
    EXPECT_EQ(0, content_block2.block_id);
    EXPECT_EQ(10, content_block2.len_block);
    EXPECT_STREQ("test1test1", content_block2.buf_block);
    EXPECT_EQ(0, content_block2.n_line);

    destroy_content_block(&content_block2);
    destroy_content_block(&content_block);
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

    FD = open("log.test_pttdb_content_block.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
    dup2(FD, 2);

    const char *db_name[] = {
        "test_post",      //MONGO_POST_DBNAME
        "test",           //MONGO_TEST_DBNAME
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
