#include "gtest/gtest.h"
#include "test.h"
#include "cmpttui/pttui_buffer.h"
#include "cmpttui/pttui_buffer_private.h"
#include "cmmigrate_pttdb.h"
#include "cmpttdb.h"

TEST(pttui_buffer, pttui_buffer_is_end_ne)
{
    PttUIBuffer buffer = {};
    buffer.is_to_delete = true;

    EXPECT_EQ(true, pttui_buffer_is_end_ne(&buffer, NULL));

    buffer.is_to_delete = false;

    EXPECT_EQ(true, pttui_buffer_is_end_ne(&buffer, NULL));
}

TEST(pttui_buffer, pttui_buffer_next_ne)
{
    PttUIBuffer *buffer = NULL;
    PttUIBuffer *next = pttui_buffer_next_ne(buffer, NULL);
    EXPECT_EQ(NULL, next);
}

TEST(pttui_buffer, pttui_buffer_next_ne2)
{
    PttUIBuffer buffer = {};
    PttUIBuffer *next = pttui_buffer_next_ne(&buffer, NULL);
    EXPECT_EQ(NULL, next);
}

TEST(pttui_buffer, pttui_buffer_next_ne3)
{
    PttUIBuffer buffer = {};
    PttUIBuffer next = {};
    buffer.next = &next;
    PttUIBuffer *next2 = pttui_buffer_next_ne(&buffer, NULL);
    EXPECT_EQ(&next, next2);
}

TEST(pttui_buffer, pttui_buffer_next_ne4)
{
    PttUIBuffer buffer = {};
    PttUIBuffer next = {};
    PttUIBuffer next2 = {};
    buffer.next = &next;
    next.next = &next2;
    PttUIBuffer *next3 = pttui_buffer_next_ne(&buffer, NULL);
    EXPECT_EQ(&next, next3);
}

TEST(pttui_buffer, pttui_buffer_next_ne5)
{
    PttUIBuffer buffer = {};
    PttUIBuffer next = {};
    PttUIBuffer next2 = {};
    buffer.next = &next;
    next.next = &next2;
    next.is_to_delete = true;
    PttUIBuffer *next3 = pttui_buffer_next_ne(&buffer, NULL);
    EXPECT_EQ(&next2, next3);
}

TEST(pttui_buffer, pttui_buffer_next_ne6)
{
    PttUIBuffer buffer = {};
    PttUIBuffer next = {};
    PttUIBuffer next2 = {};
    PttUIBuffer next3 = {};
    buffer.next = &next;
    next.next = &next2;
    next2.next = &next3;
    next2.is_to_delete = true;
    PttUIBuffer *next4 = pttui_buffer_next_ne(&buffer, NULL);
    EXPECT_EQ(&next, next4);
}

TEST(pttui_buffer, pttui_buffer_next_ne7)
{
    PttUIBuffer buffer = {};
    PttUIBuffer next = {};
    PttUIBuffer next2 = {};
    PttUIBuffer next3 = {};
    buffer.next = &next;
    next.next = &next2;
    next2.next = &next3;
    next.is_to_delete = true;
    next2.is_to_delete = true;
    PttUIBuffer *next4 = pttui_buffer_next_ne(&buffer, NULL);
    EXPECT_EQ(&next3, next4);
}

TEST(pttui_buffer, pttui_buffer_next_ne8)
{
    PttUIBuffer buffer = {};
    PttUIBuffer next = {};
    PttUIBuffer next2 = {};
    PttUIBuffer next3 = {};
    buffer.next = &next;
    next.next = &next2;
    next2.next = &next3;
    next.is_to_delete = true;
    next2.is_to_delete = true;
    next3.is_to_delete = true;
    PttUIBuffer *next4 = pttui_buffer_next_ne(&buffer, NULL);
    EXPECT_EQ(NULL, next4);
}

TEST(pttui_buffer, pttui_buffer_pre_ne)
{
    PttUIBuffer *buffer = NULL;
    PttUIBuffer *pre = pttui_buffer_pre_ne(buffer, NULL);
    EXPECT_EQ(NULL, pre);
}

TEST(pttui_buffer, pttui_buffer_pre_ne2)
{
    PttUIBuffer buffer = {};
    PttUIBuffer *pre = pttui_buffer_pre_ne(&buffer, NULL);
    EXPECT_EQ(NULL, pre);
}

TEST(pttui_buffer, pttui_buffer_pre_ne3)
{
    PttUIBuffer buffer = {};
    PttUIBuffer pre = {};
    buffer.pre = &pre;
    PttUIBuffer *pre2 = pttui_buffer_pre_ne(&buffer, NULL);
    EXPECT_EQ(&pre, pre2);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre2)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_MAIN;
    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre3)
{
    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_MAIN;
    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre4)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT;
    buffer.content_type = PTTDB_CONTENT_TYPE_MAIN;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre5)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    buffer.content_type = PTTDB_CONTENT_TYPE_MAIN;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre6)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_MAIN;
    state.top_line_block_offset = 0;
    buffer.content_type = PTTDB_CONTENT_TYPE_MAIN;
    buffer.block_offset = 1;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre7)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_MAIN;
    state.top_line_block_offset = 1;
    buffer.content_type = PTTDB_CONTENT_TYPE_MAIN;
    buffer.block_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre8)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_MAIN;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 0;
    buffer.content_type = PTTDB_CONTENT_TYPE_MAIN;
    buffer.block_offset = 0;
    buffer.line_offset = 1;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre9)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_MAIN;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 1;
    buffer.content_type = PTTDB_CONTENT_TYPE_MAIN;
    buffer.block_offset = 0;
    buffer.line_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre10)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_MAIN;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 0;
    buffer.content_type = PTTDB_CONTENT_TYPE_MAIN;
    buffer.block_offset = 0;
    buffer.line_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre11)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 0;
    state.top_line_comment_offset = 0;

    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT;
    buffer.block_offset = 0;
    buffer.line_offset = 0;
    buffer.comment_offset = 1;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre12)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 0;
    state.top_line_comment_offset = 1;

    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    buffer.block_offset = 0;
    buffer.line_offset = 0;
    buffer.comment_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre13)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 0;
    state.top_line_comment_offset = 0;

    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    buffer.block_offset = 0;
    buffer.line_offset = 0;
    buffer.comment_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre14)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 0;
    state.top_line_comment_offset = 0;

    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT;
    buffer.block_offset = 0;
    buffer.line_offset = 0;
    buffer.comment_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre15)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 0;
    state.top_line_comment_offset = 0;

    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    buffer.block_offset = 1;
    buffer.line_offset = 0;
    buffer.comment_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre16)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    state.top_line_block_offset = 1;
    state.top_line_line_offset = 0;
    state.top_line_comment_offset = 0;

    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    buffer.block_offset = 0;
    buffer.line_offset = 0;
    buffer.comment_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre17)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 0;
    state.top_line_comment_offset = 0;

    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    buffer.block_offset = 0;
    buffer.line_offset = 1;
    buffer.comment_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(true, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre18)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 1;
    state.top_line_comment_offset = 0;

    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    buffer.block_offset = 0;
    buffer.line_offset = 0;
    buffer.comment_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}


TEST(pttui_buffer, sync_pttui_buffer_info_is_pre19)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 0;
    state.top_line_comment_offset = 0;

    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;
    buffer.block_offset = 0;
    buffer.line_offset = 0;
    buffer.comment_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}

TEST(pttui_buffer, sync_pttui_buffer_info_is_pre20)
{

    PttUIState state = {};
    PttUIBuffer buffer = {};
    bool is_pre = false;

    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 0;
    state.top_line_comment_offset = 0;

    buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT;
    buffer.block_offset = 0;
    buffer.line_offset = 0;
    buffer.comment_offset = 0;

    Err error = _sync_pttui_buffer_info_is_pre(&state, &buffer, &is_pre);
    EXPECT_EQ(S_OK, error);
    EXPECT_EQ(false, is_pre);
}

TEST(pttui_buffer, pttui_buffer_init_buffer_no_buf_from_file_info)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    fprintf(stderr, "test_pttui_buffer.pttui_buffer_info_init_buffer_no_buf_from_file_info: after db force drop\n");

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    fprintf(stderr, "test_pttui_buffer.pttui_buffer_info_init_buffer_no_buf_from_file_info: to migrate file to pttdb\n");
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    fprintf(stderr, "test_pttui_buffer.pttui_buffer_info_init_buffer_no_buf_from_file_info: after migrate file to pttdb\n");

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    fprintf(stderr, "test_pttui_buffer.pttui_buffer_info_init_buffer_no_buf_from_file_info: to construct file_info\n");
    error = construct_file_info(main_id, &file_info);
    fprintf(stderr, "test_pttui_buffer.pttui_buffer_info_init_buffer_no_buf_from_file_info: after construct file_info\n");
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

    // PttUI
    PttUIState state = {};
    PttUIBuffer *buffer = NULL;
    memcpy(state.top_line_id, file_info.main_content_id, UUIDLEN);
    memcpy(state.main_id, file_info.main_id, UUIDLEN);

    fprintf(stderr, "test_pttui_buffer.pttui_buffer_info_init_buffer_no_buf_from_file_info: to sync\n");
    error = _pttui_buffer_init_buffer_no_buf_from_file_info(&state, &file_info, &buffer);
    fprintf(stderr, "test_pttui_buffer.pttui_buffer_info_init_buffer_no_buf_from_file_info: after sync\n");
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, buffer->content_type);
    EXPECT_EQ(0, buffer->block_offset);
    EXPECT_EQ(0, buffer->line_offset);
    EXPECT_EQ(0, buffer->comment_offset);
    EXPECT_EQ(PTTDB_STORAGE_TYPE_MONGO, buffer->storage_type);
    EXPECT_EQ(0, buffer->len_no_nl);
    EXPECT_EQ(NULL, buffer->buf);
    EXPECT_EQ(0, memcmp(file_info.main_content_id, buffer->the_id, UUIDLEN));

    // free

    error = destroy_file_info(&file_info);
    EXPECT_EQ(S_OK, error);

    error = safe_free_pttui_buffer(&buffer);
    EXPECT_EQ(S_OK, error);

}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer_no_buf_main)
{
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_pre_buffer_no_buf_main(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(NULL, new_buffer);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer_no_buf_main2)
{
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.line_offset = 3;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_pre_buffer_no_buf_main(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(new_buffer->the_id, current_buffer.the_id, UUIDLEN));
    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(2, new_buffer->line_offset);
    EXPECT_EQ(0, new_buffer->comment_offset);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer_no_buf_main3)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1500464247.A.6AA";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.2.txt", (char *)"poster0", (char *)"BBSmovie", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/BBSmovie/M.1500464247.A.6AA.html", 1500464247000L, main_id);

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    error = construct_file_info(main_id, &file_info);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(main_id, file_info.main_id, UUIDLEN));
    EXPECT_STREQ((char *)"poster0", file_info.main_poster);
    EXPECT_EQ(1500464247000L, file_info.main_create_milli_timestamp);
    EXPECT_EQ(1500464247000L, file_info.main_update_milli_timestamp);
    EXPECT_EQ(2124, file_info.n_main_line);
    EXPECT_EQ(16, file_info.n_main_block);
    EXPECT_EQ(15, file_info.n_comment);
    EXPECT_EQ(3, file_info.comments[0].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[0].n_comment_reply_block);
    EXPECT_EQ(3, file_info.comments[0].comment_reply_blocks[0].n_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_block);
    EXPECT_EQ(1, file_info.comments[11].comment_reply_blocks[0].n_line);

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 2;
    current_buffer.line_offset = 0;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_pre_buffer_no_buf_main(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(new_buffer->the_id, current_buffer.the_id, UUIDLEN));
    EXPECT_EQ(1, new_buffer->block_offset);
    EXPECT_EQ(255, new_buffer->line_offset);
    EXPECT_EQ(0, new_buffer->comment_offset);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer_no_buf_comment)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1500464247.A.6AA";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.2.txt", (char *)"poster0", (char *)"BBSmovie", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/BBSmovie/M.1500464247.A.6AA.html", 1500464247000L, main_id);

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    error = construct_file_info(main_id, &file_info);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(main_id, file_info.main_id, UUIDLEN));
    EXPECT_STREQ((char *)"poster0", file_info.main_poster);
    EXPECT_EQ(1500464247000L, file_info.main_create_milli_timestamp);
    EXPECT_EQ(1500464247000L, file_info.main_update_milli_timestamp);
    EXPECT_EQ(2124, file_info.n_main_line);
    EXPECT_EQ(16, file_info.n_main_block);
    EXPECT_EQ(15, file_info.n_comment);
    EXPECT_EQ(3, file_info.comments[0].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[0].n_comment_reply_block);
    EXPECT_EQ(3, file_info.comments[0].comment_reply_blocks[0].n_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_block);
    EXPECT_EQ(1, file_info.comments[11].comment_reply_blocks[0].n_line);

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 0;
    current_buffer.comment_offset = 0;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_pre_buffer_no_buf_comment(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    log_file_info(&file_info, "test_pttui_buffer.extend_pttui_buffer_extend_pre_buffer_no_buf_comment");

    EXPECT_EQ(0, memcmp(new_buffer->the_id, file_info.main_content_id, UUIDLEN));
    EXPECT_EQ(15, new_buffer->block_offset);
    EXPECT_EQ(62, new_buffer->line_offset);
    EXPECT_EQ(0, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, new_buffer->content_type);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer_no_buf_comment2)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1500464247.A.6AA";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.2.txt", (char *)"poster0", (char *)"BBSmovie", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/BBSmovie/M.1500464247.A.6AA.html", 1500464247000L, main_id);

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    error = construct_file_info(main_id, &file_info);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(main_id, file_info.main_id, UUIDLEN));
    EXPECT_STREQ((char *)"poster0", file_info.main_poster);
    EXPECT_EQ(1500464247000L, file_info.main_create_milli_timestamp);
    EXPECT_EQ(1500464247000L, file_info.main_update_milli_timestamp);
    EXPECT_EQ(2124, file_info.n_main_line);
    EXPECT_EQ(16, file_info.n_main_block);
    EXPECT_EQ(15, file_info.n_comment);
    EXPECT_EQ(3, file_info.comments[0].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[0].n_comment_reply_block);
    EXPECT_EQ(3, file_info.comments[0].comment_reply_blocks[0].n_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_block);
    EXPECT_EQ(1, file_info.comments[11].comment_reply_blocks[0].n_line);

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 0;
    current_buffer.comment_offset = 1;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_pre_buffer_no_buf_comment(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(new_buffer->the_id, file_info.comments[0].comment_reply_id, UUIDLEN));
    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(2, new_buffer->line_offset);
    EXPECT_EQ(0, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT_REPLY, new_buffer->content_type);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer_no_buf_comment3)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1500464247.A.6AA";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.2.txt", (char *)"poster0", (char *)"BBSmovie", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/BBSmovie/M.1500464247.A.6AA.html", 1500464247000L, main_id);

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    error = construct_file_info(main_id, &file_info);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(main_id, file_info.main_id, UUIDLEN));
    EXPECT_STREQ((char *)"poster0", file_info.main_poster);
    EXPECT_EQ(1500464247000L, file_info.main_create_milli_timestamp);
    EXPECT_EQ(1500464247000L, file_info.main_update_milli_timestamp);
    EXPECT_EQ(2124, file_info.n_main_line);
    EXPECT_EQ(16, file_info.n_main_block);
    EXPECT_EQ(15, file_info.n_comment);
    EXPECT_EQ(3, file_info.comments[0].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[0].n_comment_reply_block);
    EXPECT_EQ(3, file_info.comments[0].comment_reply_blocks[0].n_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_block);
    EXPECT_EQ(1, file_info.comments[11].comment_reply_blocks[0].n_line);

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 0;
    current_buffer.comment_offset = 2;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_pre_buffer_no_buf_comment(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(new_buffer->the_id, file_info.comments[1].comment_id, UUIDLEN));
    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(0, new_buffer->line_offset);
    EXPECT_EQ(1, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT, new_buffer->content_type);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer_no_buf_comment_reply)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1500464247.A.6AA";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.2.txt", (char *)"poster0", (char *)"BBSmovie", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/BBSmovie/M.1500464247.A.6AA.html", 1500464247000L, main_id);

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    error = construct_file_info(main_id, &file_info);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(main_id, file_info.main_id, UUIDLEN));
    EXPECT_STREQ((char *)"poster0", file_info.main_poster);
    EXPECT_EQ(1500464247000L, file_info.main_create_milli_timestamp);
    EXPECT_EQ(1500464247000L, file_info.main_update_milli_timestamp);
    EXPECT_EQ(2124, file_info.n_main_line);
    EXPECT_EQ(16, file_info.n_main_block);
    EXPECT_EQ(15, file_info.n_comment);
    EXPECT_EQ(3, file_info.comments[0].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[0].n_comment_reply_block);
    EXPECT_EQ(3, file_info.comments[0].comment_reply_blocks[0].n_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_block);
    EXPECT_EQ(1, file_info.comments[11].comment_reply_blocks[0].n_line);

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 0;
    current_buffer.comment_offset = 0;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_pre_buffer_no_buf_comment_reply(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(new_buffer->the_id, file_info.comments[0].comment_id, UUIDLEN));
    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(0, new_buffer->line_offset);
    EXPECT_EQ(0, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT, new_buffer->content_type);

    // free
    safe_free((void **)&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer_no_buf_comment_reply2)
{
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 0;
    current_buffer.comment_offset = 89;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_pre_buffer_no_buf_comment_reply(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(new_buffer->the_id, file_info.comments[89].comment_id, UUIDLEN));
    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(0, new_buffer->line_offset);
    EXPECT_EQ(89, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT, new_buffer->content_type);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer_no_buf_comment_reply3)
{
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.comments[89].comment_reply_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 1;
    current_buffer.comment_offset = 89;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_pre_buffer_no_buf_comment_reply(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(new_buffer->the_id, file_info.comments[89].comment_reply_id, UUIDLEN));
    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(0, new_buffer->line_offset);
    EXPECT_EQ(89, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT_REPLY, new_buffer->content_type);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}


TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer_no_buf_comment_reply4)
{
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.comments[89].comment_reply_id, UUIDLEN);
    current_buffer.block_offset = 1;
    current_buffer.line_offset = 0;
    current_buffer.comment_offset = 89;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_pre_buffer_no_buf_comment_reply(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(new_buffer->the_id, file_info.comments[89].comment_reply_id, UUIDLEN));
    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(79, new_buffer->line_offset);
    EXPECT_EQ(89, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT_REPLY, new_buffer->content_type);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}


TEST(pttui_buffer, extend_pttui_buffer_extend_next_buffer_no_buf_main)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after migrate file to pttdb\n");

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to construct file_info\n");
    error = construct_file_info(main_id, &file_info);
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after construct file_info\n");
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 0;
    current_buffer.comment_offset = 0;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_MAIN;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_next_buffer_no_buf_main(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(1, new_buffer->line_offset);
    EXPECT_EQ(0, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, new_buffer->content_type);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_next_buffer_no_buf_main2)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1500464247.A.6AA";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.2.txt", (char *)"poster0", (char *)"BBSmovie", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/BBSmovie/M.1500464247.A.6AA.html", 1500464247000L, main_id);

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    error = construct_file_info(main_id, &file_info);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(main_id, file_info.main_id, UUIDLEN));
    EXPECT_STREQ((char *)"poster0", file_info.main_poster);
    EXPECT_EQ(1500464247000L, file_info.main_create_milli_timestamp);
    EXPECT_EQ(1500464247000L, file_info.main_update_milli_timestamp);
    EXPECT_EQ(2124, file_info.n_main_line);
    EXPECT_EQ(16, file_info.n_main_block);
    EXPECT_EQ(15, file_info.n_comment);
    EXPECT_EQ(3, file_info.comments[0].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[0].n_comment_reply_block);
    EXPECT_EQ(3, file_info.comments[0].comment_reply_blocks[0].n_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_block);
    EXPECT_EQ(1, file_info.comments[11].comment_reply_blocks[0].n_line);

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 255;
    current_buffer.comment_offset = 0;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_MAIN;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_next_buffer_no_buf_main(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(new_buffer->the_id, file_info.main_content_id, UUIDLEN));
    EXPECT_EQ(1, new_buffer->block_offset);
    EXPECT_EQ(0, new_buffer->line_offset);
    EXPECT_EQ(0, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, new_buffer->content_type);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_next_buffer_no_buf_main3)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1500464247.A.6AA";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.2.txt", (char *)"poster0", (char *)"BBSmovie", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/BBSmovie/M.1500464247.A.6AA.html", 1500464247000L, main_id);

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    error = construct_file_info(main_id, &file_info);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(main_id, file_info.main_id, UUIDLEN));
    EXPECT_STREQ((char *)"poster0", file_info.main_poster);
    EXPECT_EQ(1500464247000L, file_info.main_create_milli_timestamp);
    EXPECT_EQ(1500464247000L, file_info.main_update_milli_timestamp);
    EXPECT_EQ(2124, file_info.n_main_line);
    EXPECT_EQ(16, file_info.n_main_block);
    EXPECT_EQ(15, file_info.n_comment);
    EXPECT_EQ(3, file_info.comments[0].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[0].n_comment_reply_block);
    EXPECT_EQ(3, file_info.comments[0].comment_reply_blocks[0].n_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_total_line);
    EXPECT_EQ(1, file_info.comments[11].n_comment_reply_block);
    EXPECT_EQ(1, file_info.comments[11].comment_reply_blocks[0].n_line);

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 15;
    current_buffer.line_offset = 62;
    current_buffer.comment_offset = 0;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_MAIN;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_next_buffer_no_buf_main(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(new_buffer->the_id, file_info.comments[0].comment_id, UUIDLEN));
    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(0, new_buffer->line_offset);
    EXPECT_EQ(0, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT, new_buffer->content_type);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_next_buffer_no_buf_main4)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.997843374.A";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.5.txt", (char *)"poster0", (char *)"BBSmovie", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/b885060xx/M.997843374.A.html", 997843374000L, main_id);

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    error = construct_file_info(main_id, &file_info);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, memcmp(main_id, file_info.main_id, UUIDLEN));
    EXPECT_STREQ((char *)"poster0", file_info.main_poster);
    EXPECT_EQ(997843374000L, file_info.main_create_milli_timestamp);
    EXPECT_EQ(997843374000L, file_info.main_update_milli_timestamp);
    EXPECT_EQ(23, file_info.n_main_line);
    EXPECT_EQ(1, file_info.n_main_block);
    EXPECT_EQ(23, file_info.main_blocks[0].n_line);
    EXPECT_EQ(0, file_info.n_comment);

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 22;
    current_buffer.comment_offset = 0;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_MAIN;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_next_buffer_no_buf_main(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(NULL, new_buffer);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_next_buffer_no_buf_comment)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to migrate file to pttdb\n");
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after migrate file to pttdb\n");

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to construct file_info\n");
    error = construct_file_info(main_id, &file_info);
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after construct file_info\n");
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 0;
    current_buffer.comment_offset = 0;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_next_buffer_no_buf_comment(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(0, new_buffer->line_offset);
    EXPECT_EQ(1, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT, new_buffer->content_type);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_next_buffer_no_buf_comment2)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to migrate file to pttdb\n");
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after migrate file to pttdb\n");

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to construct file_info\n");
    error = construct_file_info(main_id, &file_info);
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after construct file_info\n");
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 0;
    current_buffer.comment_offset = 89;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_next_buffer_no_buf_comment(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(0, new_buffer->line_offset);
    EXPECT_EQ(89, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT_REPLY, new_buffer->content_type);

    // free
    safe_free_pttui_buffer(&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, sync_pttui_buffer_info_extend_next_buffer_no_buf_comment3)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to migrate file to pttdb\n");
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after migrate file to pttdb\n");

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to construct file_info\n");
    error = construct_file_info(main_id, &file_info);
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after construct file_info\n");
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 0;
    current_buffer.comment_offset = 102;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_next_buffer_no_buf_comment(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(NULL, new_buffer);

    // free
    safe_free((void **)&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, sync_pttui_buffer_info_extend_next_buffer_no_buf_comment_reply)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to migrate file to pttdb\n");
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after migrate file to pttdb\n");

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to construct file_info\n");
    error = construct_file_info(main_id, &file_info);
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after construct file_info\n");
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 0;
    current_buffer.comment_offset = 89;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_next_buffer_no_buf_comment_reply(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(1, new_buffer->line_offset);
    EXPECT_EQ(89, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT_REPLY, new_buffer->content_type);

    // free
    safe_free((void **)&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, sync_pttui_buffer_info_extend_next_buffer_no_buf_comment_reply2)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to migrate file to pttdb\n");
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after migrate file to pttdb\n");

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to construct file_info\n");
    error = construct_file_info(main_id, &file_info);
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after construct file_info\n");
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 0;
    current_buffer.line_offset = 79;
    current_buffer.comment_offset = 89;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_next_buffer_no_buf_comment_reply(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(1, new_buffer->block_offset);
    EXPECT_EQ(0, new_buffer->line_offset);
    EXPECT_EQ(89, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT_REPLY, new_buffer->content_type);

    // free
    safe_free((void **)&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, sync_pttui_buffer_info_extend_next_buffer_no_buf_comment_reply3)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to migrate file to pttdb\n");
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after migrate file to pttdb\n");

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to construct file_info\n");
    error = construct_file_info(main_id, &file_info);
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after construct file_info\n");
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

    // PttUI
    PttUIBuffer current_buffer = {};
    memcpy(current_buffer.the_id, file_info.main_content_id, UUIDLEN);
    current_buffer.block_offset = 1;
    current_buffer.line_offset = 19;
    current_buffer.comment_offset = 89;
    current_buffer.content_type = PTTDB_CONTENT_TYPE_COMMENT_REPLY;

    PttUIBuffer *new_buffer = NULL;

    error = _extend_pttui_buffer_extend_next_buffer_no_buf_comment_reply(&current_buffer, &file_info, &new_buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(0, new_buffer->block_offset);
    EXPECT_EQ(0, new_buffer->line_offset);
    EXPECT_EQ(90, new_buffer->comment_offset);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT, new_buffer->content_type);

    // free
    safe_free((void **)&new_buffer);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to migrate file to pttdb\n");
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after migrate file to pttdb\n");

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to construct file_info\n");
    error = construct_file_info(main_id, &file_info);
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after construct file_info\n");
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

    // PttUI
    PttUIState state = {};
    PttUIBufferInfo buffer_info = {};
    PttUIBuffer *buffer = NULL;

    memcpy(state.top_line_id, file_info.main_content_id, UUIDLEN);
    memcpy(state.main_id, file_info.main_id, UUIDLEN);

    error = _pttui_buffer_init_buffer_no_buf_from_file_info(&state, &file_info, &buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, buffer->content_type);
    EXPECT_EQ(0, buffer->block_offset);
    EXPECT_EQ(0, buffer->line_offset);
    EXPECT_EQ(0, buffer->comment_offset);

    PttUIBuffer *new_head_buffer = NULL;
    int n_new_buffer = 1;
    error = _extend_pttui_buffer_extend_pre_buffer(&file_info, buffer, HARD_N_PTTUI_BUFFER, &new_head_buffer, &n_new_buffer);
    EXPECT_EQ(S_OK, error);

    buffer_info.head = new_head_buffer;
    buffer_info.tail = buffer;
    buffer_info.n_buffer = n_new_buffer;

    EXPECT_EQ(1, buffer_info.n_buffer);
    EXPECT_EQ(buffer_info.head, buffer_info.tail);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, buffer_info.head->content_type);
    EXPECT_EQ(NULL, buffer_info.head->next);
    EXPECT_EQ(NULL, buffer_info.head->pre);
    EXPECT_EQ(0, buffer_info.head->block_offset);
    EXPECT_EQ(0, buffer_info.head->line_offset);
    EXPECT_EQ(0, buffer_info.head->comment_offset);

    // free
    destroy_pttui_buffer_info(&buffer_info);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer2)
{
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN);
    _DB_FORCE_DROP_COLLECTION(MONGO_MAIN_CONTENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY);
    _DB_FORCE_DROP_COLLECTION(MONGO_COMMENT_REPLY_BLOCK);

    char *filename = (char *)"M.1511576360.A.A15";
    aidu_t aid = fn2aidu(filename);

    UUID main_id = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to migrate file to pttdb\n");
    Err error = migrate_file_to_pttdb((const char *)"data_test/original_msg.4.txt", (char *)"poster0", (char *)"Gossiping", (char *)"title1", (char *)"ptt.cc", aid, (char *)"https://www.ptt.cc/bbs/Gossiping/M.1511576360.A.A15.html", 1511576360000L, main_id);

    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after migrate file to pttdb\n");

    EXPECT_EQ(S_OK, error);

    FileInfo file_info = {};
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: to construct file_info\n");
    error = construct_file_info(main_id, &file_info);
    fprintf(stderr, "test_pttui_buffer.sync_pttui_buffer_info_init_buffer_no_buf_from_file_info: after construct file_info\n");
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

    // PttUI
    PttUIState state = {};
    PttUIBufferInfo buffer_info = {};
    PttUIBuffer *buffer = NULL;

    memcpy(state.top_line_id, file_info.main_content_id, UUIDLEN);
    memcpy(state.main_id, file_info.main_id, UUIDLEN);
    state.top_line_line_offset = 40;

    error = _pttui_buffer_init_buffer_no_buf_from_file_info(&state, &file_info, &buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, buffer->content_type);
    EXPECT_EQ(0, buffer->block_offset);
    EXPECT_EQ(40, buffer->line_offset);
    EXPECT_EQ(0, buffer->comment_offset);
    EXPECT_EQ(40, buffer->load_line_offset);
    EXPECT_EQ(39, buffer->load_line_pre_offset);
    EXPECT_EQ(INVALID_LINE_OFFSET_NEXT_END, buffer->load_line_next_offset);

    PttUIBuffer *new_head_buffer = NULL;
    int n_new_buffer = 1;

    error = _extend_pttui_buffer_extend_pre_buffer(&file_info, buffer, HARD_N_PTTUI_BUFFER, &new_head_buffer, &n_new_buffer);
    EXPECT_EQ(S_OK, error);

    buffer_info.head = new_head_buffer;
    buffer_info.tail = buffer;
    buffer_info.n_buffer = n_new_buffer;

    EXPECT_EQ(41, buffer_info.n_buffer);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, buffer_info.head->content_type);
    EXPECT_EQ(NULL, buffer_info.head->pre);
    EXPECT_EQ(NULL, buffer_info.tail->next);
    EXPECT_EQ(0, buffer_info.head->block_offset);
    EXPECT_EQ(0, buffer_info.head->line_offset);
    EXPECT_EQ(0, buffer_info.head->comment_offset);

    // free
    destroy_pttui_buffer_info(&buffer_info);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_extend_pre_buffer3)
{
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

    // PttUI
    PttUIState state = {};
    PttUIBufferInfo buffer_info = {};
    PttUIBuffer *buffer = NULL;

    memcpy(state.top_line_id, file_info.comments[102].comment_id, UUIDLEN);
    memcpy(state.main_id, file_info.main_id, UUIDLEN);
    state.top_line_content_type = PTTDB_CONTENT_TYPE_COMMENT;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 0;
    state.top_line_comment_offset = 102;

    error = _pttui_buffer_init_buffer_no_buf_from_file_info(&state, &file_info, &buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT, buffer->content_type);
    EXPECT_EQ(0, buffer->block_offset);
    EXPECT_EQ(0, buffer->line_offset);
    EXPECT_EQ(102, buffer->comment_offset);
    EXPECT_EQ(0, buffer->load_line_offset);
    EXPECT_EQ(INVALID_LINE_OFFSET_PRE_END, buffer->load_line_pre_offset);
    EXPECT_EQ(INVALID_LINE_OFFSET_NEXT_END, buffer->load_line_next_offset);

    PttUIBuffer *new_head_buffer = NULL;
    int n_new_buffer = 1;

    error = _extend_pttui_buffer_extend_pre_buffer(&file_info, buffer, HARD_N_PTTUI_BUFFER, &new_head_buffer, &n_new_buffer);
    EXPECT_EQ(S_OK, error);

    buffer_info.head = new_head_buffer;
    buffer_info.tail = buffer;
    buffer_info.n_buffer = n_new_buffer;

    EXPECT_EQ(244, buffer_info.n_buffer);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, buffer_info.head->content_type);
    EXPECT_EQ(NULL, buffer_info.head->pre);
    EXPECT_EQ(NULL, buffer_info.tail->next);
    EXPECT_EQ(0, buffer_info.head->block_offset);
    EXPECT_EQ(0, buffer_info.head->line_offset);
    EXPECT_EQ(0, buffer_info.head->comment_offset);

    PttUIBuffer *p = buffer_info.head;
    for(int i = 0; i < 41; i++, p = p->next) {
        EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, p->content_type);
        EXPECT_EQ(0, p->block_offset);
        EXPECT_EQ(i, p->line_offset);
        EXPECT_EQ(0, p->comment_offset);
    }
    for(int i = 0; i < 90; i++, p = p->next) {
        EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT, p->content_type);
        EXPECT_EQ(0, p->block_offset);
        EXPECT_EQ(0, p->line_offset);
        EXPECT_EQ(i, p->comment_offset);
    }
    for(int i = 0; i < 80; i++, p = p->next) {
        EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT_REPLY, p->content_type);
        EXPECT_EQ(0, p->block_offset);
        EXPECT_EQ(i, p->line_offset);
        EXPECT_EQ(89, p->comment_offset);
    }
    for(int i = 0; i < 20; i++, p = p->next) {
        EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT_REPLY, p->content_type);
        EXPECT_EQ(1, p->block_offset);
        EXPECT_EQ(i, p->line_offset);
        EXPECT_EQ(89, p->comment_offset);
    }
    for(int i = 90; i < 103; i++, p = p->next) {
        EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT, p->content_type);
        EXPECT_EQ(0, p->block_offset);
        EXPECT_EQ(0, p->line_offset);
        EXPECT_EQ(i, p->comment_offset);
    }
    EXPECT_EQ(NULL, p);

    // free
    destroy_pttui_buffer_info(&buffer_info);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, extend_pttui_buffer_info_extend_next_buffer)
{
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

    // PttUI
    PttUIState state = {};
    PttUIBufferInfo buffer_info = {};
    PttUIBuffer *buffer = NULL;

    memcpy(state.top_line_id, file_info.main_content_id, UUIDLEN);
    memcpy(state.main_id, file_info.main_id, UUIDLEN);

    error = _pttui_buffer_init_buffer_no_buf_from_file_info(&state, &file_info, &buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, buffer->content_type);
    EXPECT_EQ(0, buffer->block_offset);
    EXPECT_EQ(0, buffer->line_offset);
    EXPECT_EQ(0, buffer->comment_offset);

    PttUIBuffer *new_tail_buffer = NULL;
    int n_new_buffer = 1;
    error = _extend_pttui_buffer_extend_next_buffer(&file_info, buffer, HARD_N_PTTUI_BUFFER, &new_tail_buffer, &n_new_buffer);
    EXPECT_EQ(S_OK, error);

    buffer_info.head = buffer;
    buffer_info.tail = new_tail_buffer;
    buffer_info.n_buffer = n_new_buffer;

    EXPECT_EQ(244, buffer_info.n_buffer);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, buffer_info.head->content_type);
    EXPECT_EQ(NULL, buffer_info.head->pre);
    EXPECT_EQ(NULL, buffer_info.tail->next);
    EXPECT_EQ(0, buffer_info.head->block_offset);
    EXPECT_EQ(0, buffer_info.head->line_offset);
    EXPECT_EQ(0, buffer_info.head->comment_offset);

    // free
    destroy_pttui_buffer_info(&buffer_info);

    destroy_file_info(&file_info);
}

TEST(pttui_buffer, pttui_buffer_info_to_resource_info)
{
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

    // PttUI
    PttUIState state = {};
    PttUIBufferInfo buffer_info = {};
    PttUIBuffer *buffer = NULL;

    memcpy(state.top_line_id, file_info.main_content_id, UUIDLEN);
    memcpy(state.main_id, file_info.main_id, UUIDLEN);

    state.top_line_content_type = PTTDB_CONTENT_TYPE_MAIN;
    state.top_line_block_offset = 0;
    state.top_line_line_offset = 30;
    state.top_line_comment_offset = 0;

    error = _pttui_buffer_init_buffer_no_buf_from_file_info(&state, &file_info, &buffer);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, buffer->content_type);
    EXPECT_EQ(0, buffer->block_offset);
    EXPECT_EQ(30, buffer->line_offset);
    EXPECT_EQ(0, buffer->comment_offset);

    PttUIBuffer *new_tail_buffer = NULL;
    int n_new_buffer = 1;
    error = _extend_pttui_buffer_extend_next_buffer_no_buf(buffer, &file_info, HARD_N_PTTUI_BUFFER, &new_tail_buffer, &n_new_buffer);
    EXPECT_EQ(S_OK, error);

    buffer_info.head = buffer;
    buffer_info.tail = new_tail_buffer;
    buffer_info.n_buffer = n_new_buffer;

    EXPECT_EQ(214, buffer_info.n_buffer);
    EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, buffer_info.head->content_type);
    EXPECT_EQ(NULL, buffer_info.head->pre);
    EXPECT_EQ(NULL, buffer_info.tail->next);
    EXPECT_EQ(0, buffer_info.head->block_offset);
    EXPECT_EQ(30, buffer_info.head->line_offset);
    EXPECT_EQ(0, buffer_info.head->comment_offset);

    fprintf(stderr, "test_pttui_buffer.pttui_buffer_info_to_resource_info: to loop buffer_info\n");

    PttUIBuffer *p = buffer_info.head;
    for(int i = 30; i < 41; i++, p = p->next) {
        EXPECT_EQ(PTTDB_CONTENT_TYPE_MAIN, p->content_type);
        EXPECT_EQ(0, p->block_offset);
        EXPECT_EQ(i, p->line_offset);
        EXPECT_EQ(0, p->comment_offset);
    }
    for(int i = 0; i < 90; i++, p = p->next) {
        EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT, p->content_type);
        EXPECT_EQ(0, p->block_offset);
        EXPECT_EQ(0, p->line_offset);
        EXPECT_EQ(i, p->comment_offset);
    }
    for(int i = 0; i < 80; i++, p = p->next) {
        EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT_REPLY, p->content_type);
        EXPECT_EQ(0, p->block_offset);
        EXPECT_EQ(i, p->line_offset);
        EXPECT_EQ(89, p->comment_offset);
    }
    for(int i = 0; i < 20; i++, p = p->next) {
        EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT_REPLY, p->content_type);
        EXPECT_EQ(1, p->block_offset);
        EXPECT_EQ(i, p->line_offset);
        EXPECT_EQ(89, p->comment_offset);
    }
    for(int i = 90; i < 103; i++, p = p->next) {
        EXPECT_EQ(PTTDB_CONTENT_TYPE_COMMENT, p->content_type);
        EXPECT_EQ(0, p->block_offset);
        EXPECT_EQ(0, p->line_offset);
        EXPECT_EQ(i, p->comment_offset);
    }
    EXPECT_EQ(NULL, p);

    fprintf(stderr, "test_pttui_buffer.pttui_buffer_info_to_resource_info: after loop buffer_info\n");

    PttUIResourceInfo resource_info = {};
    error = _pttui_buffer_info_to_resource_info(buffer_info.head, buffer_info.tail, &resource_info);
    EXPECT_EQ(S_OK, error);

    EXPECT_EQ(1, resource_info.queue[PTTDB_CONTENT_TYPE_MAIN * N_PTTDB_STORAGE_TYPE + PTTDB_STORAGE_TYPE_MONGO].n_queue);
    EXPECT_EQ(103, resource_info.queue[PTTDB_CONTENT_TYPE_COMMENT * N_PTTDB_STORAGE_TYPE + PTTDB_STORAGE_TYPE_MONGO].n_queue);
    EXPECT_EQ(2, resource_info.queue[PTTDB_CONTENT_TYPE_COMMENT_REPLY * N_PTTDB_STORAGE_TYPE + PTTDB_STORAGE_TYPE_MONGO].n_queue);

    // free
    destroy_pttui_buffer_info(&buffer_info);

    destroy_pttui_resource_info(&resource_info);

    destroy_file_info(&file_info);
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

    pttui_thread_lock_init();

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

    FD = open("log.test_pttui_buffer.err", O_WRONLY | O_CREAT | O_TRUNC, 0660);
    dup2(FD, 2);

}

void MyEnvironment::TearDown() {
    free_mongo_collections();
    free_mongo_global();

    pttui_thread_lock_destroy();

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

