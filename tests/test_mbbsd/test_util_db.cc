#include "gtest/gtest.h"
#include "bbs.h"
#include "ptterr.h"
#include "util_db.h"
#include "util_db_internal.h"

TEST(util_db, db_set_if_not_exists) {
    Err error;

    _DB_FORCE_DROP_COLLECTION(MONGO_TEST);

    bson_t *b = BCON_NEW("test", BCON_INT32(1));

    error = db_set_if_not_exists(MONGO_TEST, b);
    EXPECT_EQ(S_OK, error);

    if(!error) {
        error = db_set_if_not_exists(MONGO_TEST, b);
        EXPECT_EQ(S_ERR_ALREADY_EXISTS, error);
    }

    _DB_FORCE_DROP_COLLECTION(MONGO_TEST);

    bson_safe_destroy(&b);

}

TEST(util_db, db_update_one) {
    Err error;

    _DB_FORCE_DROP_COLLECTION(MONGO_TEST);

    bson_t *key = BCON_NEW("the_key", BCON_BINARY((const unsigned char *)"key0", 4));
    bson_t *val = BCON_NEW("the_val", BCON_BINARY((const unsigned char *)"val0", 4));

    error = db_update_one(MONGO_TEST, key, val, true);
    EXPECT_EQ(S_OK, error);

    _DB_FORCE_DROP_COLLECTION(MONGO_TEST);

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);

}

TEST(util_db, db_find_one) {
    Err error;

    _DB_FORCE_DROP_COLLECTION(MONGO_TEST);

    bson_t *key = BCON_NEW("the_key", BCON_INT32(4));
    bson_t *val = BCON_NEW("the_val", BCON_INT32(5));

    error = db_update_one(MONGO_TEST, key, val, true);
    EXPECT_EQ(S_OK, error);

    // result
    bson_t *result = NULL;

    fprintf(stderr, "test_db_find_one: to db_find_one\n");
    error = db_find_one(MONGO_TEST, key, NULL, &result);
    EXPECT_EQ(S_OK, error);

    int int_result;
    error = bson_get_value_int32(result, (char *)"the_key", &int_result);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(4, int_result);

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);
    bson_safe_destroy(&result);
}

TEST(util_db, db_find_one2_with_fields) {
    Err error;

    _DB_FORCE_DROP_COLLECTION(MONGO_TEST);

    bson_t *key = BCON_NEW("the_key", BCON_INT32(4));
    bson_t *val = BCON_NEW("the_val", BCON_INT32(5));

    error = db_update_one(MONGO_TEST, key, val, true);
    EXPECT_EQ(S_OK, error);

    int int_result;
    bson_t *fields = BCON_NEW("the_val", BCON_BOOL(true));
    bson_t *result = NULL;

    error = db_find_one(MONGO_TEST, key, fields, &result);
    EXPECT_EQ(S_OK, error);

    if(!error) {
        error = bson_get_value_int32(result, (char *)"the_key", &int_result);
        EXPECT_EQ(S_ERR, error);
    }

    if(!error) {
        error = bson_get_value_int32(result, (char *)"the_val", &int_result);
        EXPECT_EQ(S_OK, error);
        EXPECT_EQ(int_result, 5);
    }

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);
    bson_safe_destroy(&fields);
    bson_safe_destroy(&result);
}

TEST(util_db, db_remove) {
    Err error;

    _DB_FORCE_DROP_COLLECTION(MONGO_TEST);

    bson_t *key = BCON_NEW("the_key", BCON_INT32(4));
    bson_t *val = BCON_NEW("the_val", BCON_INT32(5));

    error = db_update_one(MONGO_TEST, key, val, true);
    EXPECT_EQ(S_OK, error);

    bson_t *result = NULL;

    error = db_remove(MONGO_TEST, key);
    EXPECT_EQ(S_OK, error);

    error = db_find_one(MONGO_TEST, key, NULL, &result);
    EXPECT_EQ(S_ERR_NOT_EXISTS, error);
    EXPECT_EQ(NULL, result);

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);
    bson_safe_destroy(&result);
}

TEST(util_db, db_aggregate) {
    Err error = S_OK;

    _DB_FORCE_DROP_COLLECTION(MONGO_TEST);

    bson_t *key = BCON_NEW("tmp", BCON_INT32(4));
    bson_t *val = BCON_NEW(
        "len", BCON_INT32(5),
        "main_id", BCON_INT32(4)
        );

    error = db_update_one(MONGO_TEST, key, val, true);
    EXPECT_EQ(S_OK, error);

    bson_t *key2 = BCON_NEW("tmp", BCON_INT32(5));
    bson_t *val2 = BCON_NEW(
        "len", BCON_INT32(10),
        "main_id", BCON_INT32(4)
        );

    error = db_update_one(MONGO_TEST, key2, val2, true);
    EXPECT_EQ(S_OK, error);

    bson_t *pipeline = BCON_NEW(
        "pipeline", "[",
            "{",
                "$match",
                "{",
                    "main_id", BCON_INT32(4),
                "}",
            "}",
            "{",
                "$group",
                "{",
                    "_id", BCON_NULL,
                    "count", "{",
                        "$sum", BCON_INT32(1),
                    "}",
                    "len", "{",
                        "$sum", "$len",
                    "}",
                "}",
            "}",
        "]"
        );    

    char *str = bson_as_canonical_extended_json(pipeline, NULL);
    fprintf(stderr, "test_util_db.db_aggregate: pipeline: %s\n", str);
    bson_free(str);

    bson_t *result = NULL;

    int n_result;
    error = db_aggregate(MONGO_TEST, pipeline, 1, &result, &n_result);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(1, n_result);

    str = bson_as_canonical_extended_json(result, NULL);
    fprintf(stderr, "test_util_db.db_aggregate: result: %s\n", str);
    bson_free(str);

    int count = 0;
    int len = 0;

    error = bson_get_value_int32(result, (char *)"count", &count);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(2, count);

    error = bson_get_value_int32(result, (char *)"len", &len);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(15, len);

    bson_safe_destroy(&pipeline);

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);

    bson_safe_destroy(&key2);
    bson_safe_destroy(&val2);

    bson_safe_destroy(&result);
}

TEST(util_db, db_count) {
    Err error = S_OK;

    _DB_FORCE_DROP_COLLECTION(MONGO_TEST);

    bson_t *key = BCON_NEW("tmp", BCON_INT32(4));
    bson_t *val = BCON_NEW(
        "len", BCON_INT32(5),
        "main_id", BCON_INT32(4)
        );

    error = db_update_one(MONGO_TEST, key, val, true);
    EXPECT_EQ(S_OK, error);

    bson_t *key2 = BCON_NEW("tmp", BCON_INT32(5));
    bson_t *val2 = BCON_NEW(
        "len", BCON_INT32(10),
        "main_id", BCON_INT32(4)
        );

    error = db_update_one(MONGO_TEST, key2, val2, true);
    EXPECT_EQ(S_OK, error);

    int count;

    bson_t *query_key = BCON_NEW(
        "main_id", BCON_INT32(4)
        );

    error = db_count(MONGO_TEST, query_key, &count);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(2, count);

    bson_safe_destroy(&query_key);

    bson_safe_destroy(&key);
    bson_safe_destroy(&val);

    bson_safe_destroy(&key2);
    bson_safe_destroy(&val2);
}

TEST(util_db, bson_safe_destroy) {
    bson_t *b = NULL;
    Err error = bson_safe_destroy(&b);
    EXPECT_EQ(S_OK, error);
}

TEST(util_db, bson_safe_destroy2) {
    bson_t *b = bson_new();
    Err error = bson_safe_destroy(&b);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(NULL, b);
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

    FD = open("log.test_util_db.err", O_WRONLY|O_CREAT|O_TRUNC, 0660);
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
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new MyEnvironment);

    return RUN_ALL_TESTS();
}
