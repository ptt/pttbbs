/* $Id$ */
#ifndef PTTDB_H
#define PTTDB_H

#ifdef __cplusplus
extern "C" {
#endif

#define UUIDLEN 64
#define _UUIDLEN 48

#define MAX_ORIGIN_LEN 20
#define MAX_WEB_LINK 50
#define MAX_BUF_SIZE 8192
#define MAX_BUF_BLOCK 8192
#define MAX_BUF_COMMENT 256
#define MAX_BUF_LINES 256

#define N_GEN_UUID_WITH_DB 10

// Mongo Post
#define MONGO_POST_DBNAME "post"

#define MONGO_MAIN_NAME "main"
#define MONGO_MAIN_CONTENT_NAME "main_content"

// Mongo Test
#define MONGO_TEST_DBNAME "test"

#define MONGO_TEST_NAME "test"

// Mongo the id
#define MONGO_THE_ID "the_id"
#define MONGO_BLOCK_ID "block_id"

// XXX hack for time64_t and UUID
typedef long long int time64_t;
typedef unsigned char UUID[UUIDLEN];
typedef unsigned char _UUID[_UUIDLEN];

enum {
    MONGO_MAIN,
    MONGO_MAIN_CONTENT,

    MONGO_TEST,

    N_MONGO_COLLECTIONS,
};


enum {
    COMMENTTYPE_GOOD,
    COMMENTTYPE_BAD,
    COMMENTTYPE_ARROW,
    COMMENTTYPE_SIZE,

    COMMENTTYPE_FORWARD,                       // hack for forward
    COMMENTTYPE_OTHER,                         // hack for other

    COMMENTTYPE_MAX     = COMMENTTYPE_SIZE - 1,
    COMMENTTYPE_DEFAULT = COMMENTTYPE_GOOD,
};

enum {
    KARMA_GOOD = 1,
    KARMA_BAD = -1,
    KARMA_ARROW = 0,
    KARMA_FORWARD = 0,
    KARMA_OTHER = 0,
};

enum {
    LIVE_STATUS_ALIVE,
    LIVE_STATUS_DELETED,
};

/**********
 * Main
 * XXX always update main-content first, and then update main-header.
 **********/
typedef struct MainHeader {
    unsigned char version;

    UUID the_id;
    UUID content_id;
    UUID update_content_id;
    aidu_t aid;

    unsigned char status;
    char status_updater[IDLEN + 1];
    unsigned char status_update_ip[IPV4LEN + 1];

    char title[TTLEN + 1];

    char poster[IDLEN + 1];
    unsigned char ip[IPV4LEN + 1];
    time64_t create_milli_timestamp;
    char updater[IDLEN + 1];
    unsigned char update_ip[IPV4LEN + 1];
    time64_t update_milli_timestamp;

    char origin[MAX_ORIGIN_LEN + 1];
    char web_link[MAX_WEB_LINK + 1];

    int reset_karma;

    int n_total_line;
    int n_total_block;
    int len_total;
} MainHeader;

typedef struct MainContent {
    UUID the_id;
    UUID main_id;

    int block_id;

    int len_block;
    int n_line;

    char buf_block[MAX_BUF_BLOCK + 1];
} MainContent;

/**********
 * Comments
 **********/
typedef struct Comment {
    unsigned char version;

    UUID the_id;
    UUID main_id;

    unsigned char status;
    char status_updater[IDLEN + 1];
    unsigned char status_update_ip[IPV4LEN + 1];

    unsigned char rec_type;
    int karma;

    char poster[IDLEN + 1];
    unsigned char ip[IPV4LEN + 1];
    time64_t create_milli_timestamp;
    char updater[IDLEN + 1];
    unsigned char update_ip[IPV4LEN + 1];
    time64_t update_milli_timestamp;

    int len;
    char buf[MAX_BUF_COMMENT + 1];
} Comment;

/**********
 * CommentReply
 * XXX always update comment-reply-content first, and then update comment-reply-header.
 **********/
typedef struct CommentReplyHeader {
    unsigned char version;

    UUID the_id;
    UUID content_id;
    UUID update_content_id;

    UUID comment_id;
    UUID main_id;

    unsigned char status;
    char status_updater[IDLEN + 1];
    unsigned char status_update_ip[IPV4LEN + 1];

    char poster[IDLEN + 1];
    unsigned char ip[IPV4LEN + 1];
    time64_t create_milli_timestamp;
    char updater[IDLEN + 1];
    unsigned char update_ip[IPV4LEN + 1];
    time64_t update_milli_timestamp;

    int n_total_line;
    int n_total_block;
    int len_total;
} CommentReplyHeader;

typedef struct CommentReplyContent {
    UUID the_id;
    UUID comment_reply_id;

    int block_id;

    int len_block;
    int n_line;

    char buf_block[MAX_BUF_BLOCK + 1];
} CommentReplyContent;


/**********
 * Milli-timestamp
 **********/
Err get_milli_timestamp(time64_t *milli_timestamp);

/**********
 * UUID
 **********/
Err gen_uuid(UUID uuid);
Err gen_uuid_with_db(int collection, UUID uuid);
Err gen_content_uuid_with_db(int collection, UUID uuid);

Err uuid_to_milli_timestamp(UUID uuid, time64_t *milli_timestamp);

/**********
 * Mongo
 **********/
Err init_mongo_global();
Err free_mongo_global();
Err init_mongo_collections();
Err free_mongo_collections();

/**********
 * Post
 **********/
/*
Err n_line_post(UUID main_id, int *n_line);
*/

/**********
 * Main
 **********/
Err create_main_from_fd(aidu_t aid, char *title, char *poster, unsigned char *ip, unsigned char *origin, unsigned char *web_link, int len, int fd_content, UUID main_id);

/*
Err len_main(UUID main_id);
Err len_main_by_aid(aidu_t aid);

Err n_line_main(UUID main_id);
Err n_line_main_by_aid(aidu_t aid);

Err read_main_header(UUID main_id, MainHeader *main_header);
Err read_main_header_by_aid(aidu_t aid, MainHeader *main);
Err read_main_contents(UUID main_content_id, int block_id, int max_n_main_content, int *n_read_main_content, MainContent *main_content);

Err check_main(UUID main_id);

Err update_main(UUID main_id, char *updater, unsigned char *ip, int len, char *content);
Err update_main_by_aid(aidu_t aid, char *updater, unsigned char *ip, int len, char *content);

Err delete_main(UUID main_id, char *updater, unsigned char *ip);
Err delete_main_by_aid(aidu_t aid, char *updater, unsigned char *ip);
*/

/**********
 * Comments
 **********/
/*
Err create_comment(UUID main_id, char *poster, unsigned char *ip, int len, char *content, UUID *comment_id);

Err count_karma_by_main(UUID main_id);
Err len_comments_by_main(UUID main_id);
Err n_line_comments_by_main(UUID main_id);
Err read_comments_by_main(UUID main_id, time4_t create_timestamp, char *poster, int max_n_comments, int *n_read_comments, Comment *comments);
Err read_comments_by_main_aid(aidu_t aid, time4_t create_timestamp, char *poster, int max_n_comments, int *n_read_comments, Comment *comments);

Err update_comment(UUID comment_id, char *updater, unsigned char *ip, int len, char *content);

Err delete_comment(UUID the_id, char *updater, unsigned char *ip);
*/

/**********
 * CommentReply
 **********/
/*
Err create_comment_reply(UUID main_id, UUID comment_id, char *poster, unsigned char *ip, int len, char *content, UUID *comment_reply_id);

Err len_comment_reply_by_main(UUID main_id);

Err n_line_comment_reply_by_main(UUID main_id);
Err n_line_comment_reply_by_comment(UUID comment_id);
Err n_line_comment_reply_by_comment_reply(UUID comment_reply_id);

Err read_comment_reply_header_by_comment(UUID comment_id, CommentReplyHeader *comment_reply_header);
Err read_comment_reply_header_by_comment_reply_id(UUID comment_reply_id, CommentReplyHeader *comment_reply_header);
Err read_comment_reply_contents(UUID comment_reply_content_id, int block_id, int max_n_comment_reply_content, int *n_read_comment_reply_content, CommentReplyContent * comment_reply_content);

Err check_comment_reply(UUID comment_reply_id);

Err update_comment_reply(UUID comment_reply_id, char *updater, unsigned char *ip, int len, char *content);

Err delete_comment(UUID comment_reply_id, char *updater, unsigned char *ip);
*/

#ifdef __cplusplus
}
#endif

#endif /* PTTDB_H */