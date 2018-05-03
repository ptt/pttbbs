#include "gtest/gtest.h"
#include "test.h"
#include "ptterr.h"
#include "cmpttdb/pttdb_dict_bson_by_uu.h"
#include "cmpttdb/pttdb_dict_bson_by_uu_private.h"
#include "pttstruct.h"

TEST(pttdb_dict_bson_by_uu, add_to_dict_bson_by_uu) {
    DictBsonByUU dict_bson_by_uu = {};

    Err error = init_dict_bson_by_uu(&dict_bson_by_uu, 100);

    UUID uuid = {};
    gen_uuid(uuid, 0);

    bson_t *b = BCON_NEW(
        "the_id", BCON_BINARY(uuid, UUIDLEN),
        "poster", BCON_BINARY((unsigned char *)"poster0", 7)
        );

    error = add_to_dict_bson_by_uu(uuid, b, &dict_bson_by_uu);
    EXPECT_EQ(S_OK, error);

    char poster[IDLEN + 1] = {};
    bson_t *b2 = NULL;
    error = get_bson_from_dict_bson_by_uu(&dict_bson_by_uu, uuid, &b2);
    EXPECT_EQ(b, b2);
    int len = 0;
    error = bson_get_value_bin(b2, (char *)"poster", IDLEN, poster, &len);
    EXPECT_STREQ(poster, "poster0");

    // free
    bson_safe_destroy(&b);

    safe_destroy_dict_bson_by_uu(&dict_bson_by_uu);
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
    FD = open("log.test_pttdb_dict_bson_by_uu.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
    dup2(FD, 2);

}

void MyEnvironment::TearDown() {
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
