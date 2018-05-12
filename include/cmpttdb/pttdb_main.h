/* $Id$ */
#ifndef PTTDB_MAIN_H
#define PTTDB_MAIN_H

#include "ptterr.h"
#include "cmpttdb/pttdb_const.h"
#include "cmpttdb/pttdb_uuid.h"
#include "cmpttdb/pttdb_file_info.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "pttstruct.h"
#include "chess.h" // XXX hack for proto.h
#include "fav.h"   // XXX hack for proto.h
#include "proto.h" // XXX for aidu_t

#define MAX_ORIGIN_LEN 20
#define MAX_WEB_LINK_LEN 100                         // MAX_ORIGN_LEN + 8 + 12 + BOARDLEN + 1 + 23

typedef struct MainHeader {
    unsigned int version;                            // version

    UUID the_id;                                     // main id
    UUID content_id;                                 // corresponding content-id
    UUID update_content_id;                          // updating content-id, not effective if content_id == update_content_id
    aidu_t aid;                                      // aid

    enum LiveStatus status;                          // status of the main.
    char status_updater[IDLEN + 1];                  // last user updating the status
    char status_update_ip[IPV4LEN + 1];              // last ip updating the status

    char board[IDLEN + 1];                           // board-id
    char title[TTLEN + 1];                           // title

    char poster[IDLEN + 1];                          // creater
    char ip[IPV4LEN + 1];                            // create-ip
    time64_t create_milli_timestamp;                 // create-time
    char updater[IDLEN + 1];                         // last updater
    char update_ip[IPV4LEN + 1];                     // last update-ip
    time64_t update_milli_timestamp;                 // last update-time

    char origin[MAX_ORIGIN_LEN + 1];                 // origin
    char web_link[MAX_WEB_LINK_LEN + 1];                 // web-link

    int reset_karma;                                 // reset-karma.

    int n_total_line;                                // total-line
    int n_total_block;                               // total-block
    int len_total;                                   // total-size
} MainHeader;


Err create_main_from_fd(aidu_t aid, char *board, char *title, char *poster, char *ip, char *origin, char *web_link, int len, int fd_content, UUID main_id, UUID content_id, time64_t create_milli_timestamp);

Err len_main(UUID main_id, int *len);
Err len_main_by_aid(aidu_t aid, int *len);

Err n_line_main(UUID main_id, int *n_line);
Err n_line_main_by_aid(aidu_t aid, int *n_line);

Err read_main_header(UUID main_id, MainHeader *main_header);
Err read_main_header_by_aid(aidu_t aid, MainHeader *main_header);

Err delete_main(UUID main_id, char *updater, char *ip);
Err delete_main_by_aid(aidu_t aid, char *updater, char *ip);

Err update_main_from_fd(UUID main_id, char *updater, char *update_ip, int len, int fd_content, UUID content_id);

Err update_main_from_content_block_infos(UUID main_id, char *updater, char *update_ip, UUID orig_content_id, int n_orig_content_block, ContentBlockInfo *content_blocks, UUID content_id, time64_t update_milli_timestamp);

Err update_main(UUID main_id, UUID content_id, char *updater, char *update_ip, time64_t update_milli_timestamp, int n_line, int n_block, int len);


Err read_main_header_to_bson(UUID main_id, bson_t *fields, bson_t **b_main);

Err serialize_main_bson(MainHeader *main_header, bson_t **main_bson);
Err deserialize_main_bson(bson_t *main_bson, MainHeader *main_header);
Err serialize_update_main_bson(UUID content_id, char *updater, char *update_ip, time64_t update_milli_timestamp, int n_total_line, int n_total_block, int len_total, bson_t **main_bson);

#ifdef __cplusplus
}
#endif

#endif /* PTTDB_MAIN_H */