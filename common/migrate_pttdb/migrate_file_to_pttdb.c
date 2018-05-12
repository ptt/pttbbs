#include "cmmigrate_pttdb/migrate_file_to_pttdb.h"
#include "cmmigrate_pttdb/migrate_file_to_pttdb_private.h"

int
get_web_url(const boardheader_t *bp, const fileheader_t *fhdr, char *buf,
          size_t szbuf)
{
    const char *folder = bp->brdname, *fn = fhdr->filename, *ext = ".html";

/*
#ifdef USE_AID_URL
    char aidc[32] = "";
    aidu_t aidu = fn2aidu(fhdr->filename);
    if (!aidu)
    return 0;

    aidu2aidc(aidc, aidu);
    fn = aidc;
    ext = "";
#endif
*/

    if (!fhdr || !*fhdr->filename || *fhdr->filename == 'L' ||
        *fhdr->filename == '.')
    return 0;

    return snprintf(buf, szbuf, URL_PREFIX "/%s/%s%s", folder, fn, ext);
}

Err
migrate_get_file_info(const fileheader_t *fhdr, const boardheader_t *bp, char *poster, char *board, char *title, char *origin, aidu_t *aid, char *web_link, time64_t *create_milli_timestamp)
{
    memcpy(poster, fhdr->owner, strlen(fhdr->owner) - 1);
    strcpy(board, bp->brdname);
    strcpy(title, fhdr->title);
    strcpy(origin, "ptt.cc");
    *aid = fn2aidu((char *)fhdr->filename);

    bool status = get_web_url(bp, fhdr, web_link, MAX_WEB_LINK_LEN);
    if(!status) bzero(web_link, MAX_WEB_LINK_LEN);

    Err error_code = _parse_create_milli_timestamp_from_filename((char *)fhdr->filename, create_milli_timestamp);

    return error_code;
}

Err
migrate_file_to_pttdb(const char *fpath, char *poster, char *board, char *title, char *origin, aidu_t aid, char *web_link, time64_t create_milli_timestamp, UUID main_id)
{
    Err error_code = S_OK;
    LegacyFileInfo legacy_file_info = {};

    // basic info
    strcpy(legacy_file_info.poster, poster);
    strcpy(legacy_file_info.board, board);
    strcpy(legacy_file_info.title, title);
    strcpy(legacy_file_info.origin, origin);
    legacy_file_info.aid = aid;
    strcpy(legacy_file_info.web_link, web_link);    
    legacy_file_info.create_milli_timestamp = create_milli_timestamp;

    // parse
    if(!error_code) {
        error_code = parse_legacy_file(fpath, &legacy_file_info);
    }

    int fd = open(fpath, O_RDONLY);

    UUID content_id = {};

    if(!error_code) {
        error_code = create_main_from_fd(legacy_file_info.aid, legacy_file_info.board, legacy_file_info.title, legacy_file_info.poster, legacy_file_info.ip, legacy_file_info.origin, legacy_file_info.web_link, legacy_file_info.main_content_len, fd, main_id, content_id, legacy_file_info.create_milli_timestamp);
    }

    UUID comment_id = {};
    UUID comment_reply_id = {};

    //lseek(fd, legacy_file_info.main_content_len, SEEK_SET);

    char buf[MAX_MIGRATE_COMMENT_COMMENT_REPLY_BUF_SIZE] = {};

    if(!error_code) {
        for(int i = 0; i < legacy_file_info.n_comment_comment_reply; i++) {
            lseek(fd, legacy_file_info.comment_info[i].comment_offset, SEEK_SET);
            read(fd, buf, legacy_file_info.comment_info[i].comment_len);
            error_code = create_comment(
                main_id,
                legacy_file_info.comment_info[i].comment_poster,
                legacy_file_info.comment_info[i].comment_ip,
                legacy_file_info.comment_info[i].comment_len,
                buf,
                legacy_file_info.comment_info[i].comment_type,
                comment_id,
                legacy_file_info.comment_info[i].comment_create_milli_timestamp
                );
            if(error_code) break;

            // comment-reply
            if(!legacy_file_info.comment_info[i].comment_reply_len) continue;

            lseek(fd, legacy_file_info.comment_info[i].comment_reply_offset, SEEK_SET);
            error_code = create_comment_reply_from_fd(
                main_id,
                comment_id,
                legacy_file_info.comment_info[i].comment_reply_poster,
                legacy_file_info.comment_info[i].comment_reply_ip,
                legacy_file_info.comment_info[i].comment_reply_len,
                fd,
                comment_reply_id,
                legacy_file_info.comment_info[i].comment_reply_create_milli_timestamp
                );
            if(error_code) break;
        }
    }

    //free
    close(fd);

    safe_destroy_legacy_file_info(&legacy_file_info);

    return error_code;
}

Err
_parse_create_milli_timestamp_from_filename(char *filename, time64_t *create_milli_timestamp)
{
    char *p_filename_milli_timestamp = filename + 2;
    long create_timestamp = atol(p_filename_milli_timestamp);
    if(!create_timestamp) return S_ERR;

    *create_milli_timestamp = create_timestamp * 1000;

    return S_OK;
}

Err
_parse_create_milli_timestamp_from_web_link(char *web_link, time64_t *create_milli_timestamp)
{
    char *p_web_link = web_link + strlen(web_link) - LEN_WEBLINK_POSTFIX - 1 - LEN_AID_POSTFIX - 1 - LEN_AID_INFIX - 1 - LEN_AID_TIMESTAMP;
    int create_timestamp = atoi(p_web_link);
    if(create_timestamp < MIN_CREATE_TIMESTAMP) return S_ERR;

    *create_milli_timestamp = (time64_t)create_timestamp * 1000;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details [long description]
 *          parse state
 *          main-content => comment => [comment-reply] => end
 *                               <=======
 *                                                                         
 *          Can we get poster / board / title / create_milli_timestamp / origin / ip from some place?
 * @param fpath [description]
 * @param legacy_file_info [WARNING! NEED TO safe_destroy_legacy_file_info outside the function]
 */
Err
parse_legacy_file(const char *fpath, LegacyFileInfo *legacy_file_info)
{
    Err error_code = S_OK;

    int file_size = 0;
    error_code = _parse_legacy_file_get_file_size(fpath, &file_size);

    if(!error_code) {
        error_code = _parse_legacy_file_main_info(fpath, file_size, legacy_file_info);
    }

    if(!error_code) {
        error_code = _parse_legacy_file_comment_comment_reply(fpath, file_size, legacy_file_info);
    }

    return error_code;
}

Err
_parse_legacy_file_get_file_size(const char *fpath, int *file_size)
{ 
    struct stat file_state = {};
    int ret = stat(fpath, &file_state);
    if(ret) return S_ERR;

    *file_size = (int)file_state.st_size;

    return _parse_legacy_file_trim_last_empty_line(fpath, file_size);
}

/*****
 * Parse main info
 *****/

Err
_parse_legacy_file_main_info(const char *fpath, int file_size, LegacyFileInfo *legacy_file_info)
{
    Err error_code = S_OK;

    int bytes = 0;
    char buf[MAX_BUF_SIZE] = {};
    int bytes_in_line = 0;
    char line[MAX_BUF_SIZE] = {};

    int fd = open(fpath, O_RDONLY);

    enum LegacyFileStatus status = LEGACY_FILE_STATUS_MAIN_CONTENT;
    while((bytes = read(fd, buf, MAX_BUF_SIZE)) > 0) {
        bytes = bytes < file_size ? bytes : file_size;
        file_size -= bytes;
        error_code = _parse_legacy_file_main_info_core(buf, bytes, line, MAX_BUF_SIZE, &bytes_in_line, legacy_file_info, &status);
        if(error_code) break;        

        if(status == LEGACY_FILE_STATUS_COMMENT) break;
    }

    if(!error_code && bytes_in_line && status != LEGACY_FILE_STATUS_COMMENT) {        
        error_code = _parse_legacy_file_main_info_last_line(bytes_in_line, line, legacy_file_info, &status);
    }

    // free
    close(fd);

    return error_code;
}

Err
_parse_legacy_file_trim_last_empty_line(const char *fpath, int *file_size) {
    char buf[MAX_BUF_SIZE] = {};

    int fd = open(fpath, O_RDONLY);

    int offset = *file_size < 3 ? *file_size : 3;
    int ret = lseek(fd, -offset, SEEK_END);

    if(ret < 0) return S_ERR;

    int bytes = read(fd, buf, offset);

    // check \n\n
    if(bytes >= 2 && buf[bytes - 2] == '\n' && buf[bytes - 1] == '\n') {
        (*file_size) -= 1;
    }
    // check \n\r\n
    else if(bytes >= 3 && (*file_size) >= 2 && buf[bytes - 3] == '\n' && buf[bytes - 2] == '\r' && buf[bytes - 1] == '\n') {
        (*file_size) -= 2;
    }

    close(fd);

    return S_OK;
}

Err
_parse_legacy_file_main_info_core(char *buf, int bytes, char *line, int line_size, int *bytes_in_line, LegacyFileInfo *legacy_file_info, enum LegacyFileStatus *status)
{
    Err error_code = S_OK;
    int bytes_in_new_line = 0;
    for(int offset_buf = 0; offset_buf < bytes; offset_buf += bytes_in_new_line) {
        error_code = get_line_from_buf(buf, offset_buf, bytes, line, *bytes_in_line, line_size, &bytes_in_new_line);
        *bytes_in_line += bytes_in_new_line;
        if(error_code) {
            error_code = S_OK;
            break;
        }

        error_code = _parse_legacy_file_main_info_core_one_line(line, *bytes_in_line, legacy_file_info, status);

        if(error_code) break;

        if(*status == LEGACY_FILE_STATUS_COMMENT) break;

        // reset line
        bzero(line, *bytes_in_line);
        *bytes_in_line = 0;
    }

    return error_code;
}

/**
 * @brief status
 * @details 
 * 
 * @param line [description]
 * @param bytes_in_line [description]
 * @param legacy_file_info [description]
 * @param LEGACY_FILE_STATUS [description]
 */
Err
_parse_legacy_file_main_info_core_one_line(char *line, int bytes_in_line, LegacyFileInfo *legacy_file_info, enum LegacyFileStatus *status)
{
    Err error_code = S_OK;

    // XXX special treat for edit
    bool is_line_edit;
    error_code = _parse_legacy_file_is_line_edit(line, bytes_in_line, &is_line_edit);
    if(is_line_edit) return S_OK;

    // XXX special treat for comment-line
    bool is_comment_line;
    error_code = _parse_legacy_file_is_comment_line(line, bytes_in_line, &is_comment_line);
    if(error_code) return error_code;    
    if(is_comment_line) {
        *status = LEGACY_FILE_STATUS_COMMENT;
        return S_OK;
    }

    // status
    switch(*status) {
    case LEGACY_FILE_STATUS_MAIN_CONTENT:
        error_code = _parse_legacy_file_main_info_core_one_line_main_content(line, bytes_in_line, legacy_file_info, status);
        break;
    default:
        break;
    }

    return error_code;
}

Err
_parse_legacy_file_main_info_core_one_line_main_content(char *line, int bytes_in_line, LegacyFileInfo *legacy_file_info, enum LegacyFileStatus *status)
{
    Err error_code = S_OK;

    bool is_comment_line = false;
    error_code = _parse_legacy_file_is_comment_line(line, bytes_in_line, &is_comment_line);
    if(error_code) {
        *status = LEGACY_FILE_STATUS_ERROR;
        return S_ERR;
    }

    if(is_comment_line) {
        *status = LEGACY_FILE_STATUS_COMMENT;
        return S_OK;
    }

    // set main-content-len
    legacy_file_info->main_content_len += bytes_in_line;

    return S_OK;
}

Err
_parse_legacy_file_main_info_last_line(int bytes_in_line, char *line, LegacyFileInfo *legacy_file_info, enum LegacyFileStatus *status)
{
    return _parse_legacy_file_main_info_core_one_line(line, bytes_in_line, legacy_file_info, status);
}

Err
_parse_legacy_file_comment_comment_reply(const char *fpath, int file_size, LegacyFileInfo *legacy_file_info)
{
    Err error_code = S_OK;

    int n_comment_comment_reply = 0;    
    error_code = _parse_legacy_file_n_comment_comment_reply(fpath, legacy_file_info->main_content_len, file_size, &n_comment_comment_reply);

    if(!error_code && n_comment_comment_reply) {
        error_code = init_legacy_file_info_comment_comment_reply(legacy_file_info, n_comment_comment_reply);
    }

    if(!error_code && n_comment_comment_reply) {
        _parse_legacy_file_comment_comment_reply_core(fpath, file_size, legacy_file_info);
    }

    return error_code;
}

/*****
 * Parse n Comment Comment-Reply
 *****/

Err
_parse_legacy_file_n_comment_comment_reply(const char *fpath, int main_content_len, int file_size, int *n_comment_comment_reply)
{
    Err error_code = S_OK;

    int tmp_n_comment_comment_reply = 0;
    int each_n_comment_comment_reply = 0;
    int bytes = 0;
    char buf[MAX_BUF_SIZE] = {};
    int bytes_in_line = 0;
    char line[MAX_BUF_SIZE] = {};

    int fd = open(fpath, O_RDONLY);
    lseek(fd, main_content_len, SEEK_SET);
    file_size -= main_content_len;
    while((bytes = read(fd, buf, MAX_BUF_SIZE)) > 0) {
        bytes = bytes < file_size ? bytes : file_size;
        file_size -= bytes;
        each_n_comment_comment_reply = 0;
        error_code = _parse_legacy_file_n_comment_comment_reply_core(buf, bytes, line, MAX_BUF_SIZE, &bytes_in_line, &each_n_comment_comment_reply);
        if(error_code) break;

        tmp_n_comment_comment_reply += each_n_comment_comment_reply;        
    }

    if(!error_code && bytes_in_line) {
        each_n_comment_comment_reply = 0;
        error_code = _parse_legacy_file_n_comment_comment_reply_last_line(bytes_in_line, line, &each_n_comment_comment_reply);
        if(!error_code) {
            tmp_n_comment_comment_reply += each_n_comment_comment_reply;
        }
    }

    *n_comment_comment_reply = tmp_n_comment_comment_reply;

    // free
    close(fd);

    return error_code;
}


Err
_parse_legacy_file_n_comment_comment_reply_core(char *buf, int bytes, char *line, int line_size, int *bytes_in_line, int *n_comment_comment_reply)
{
    Err error_code = S_OK;
    int bytes_in_new_line = 0;
    for(int offset_buf = 0; offset_buf < bytes; offset_buf += bytes_in_new_line) {
        error_code = get_line_from_buf(buf, offset_buf, bytes, line, *bytes_in_line, line_size, &bytes_in_new_line);
        *bytes_in_line += bytes_in_new_line;
        if(error_code) {
            error_code = S_OK;
            break;
        }

        error_code = _parse_legacy_file_n_comment_comment_reply_core_one_line(line, *bytes_in_line, n_comment_comment_reply);

        if(error_code) break;

        // reset line
        bzero(line, *bytes_in_line);
        *bytes_in_line = 0;
    }

    return error_code;
}

Err
_parse_legacy_file_n_comment_comment_reply_core_one_line(char *line, int bytes_in_line, int *n_comment_comment_reply)
{
    Err error_code = S_OK;
    // XXX special treat for edit
    bool is_line_edit;
    error_code = _parse_legacy_file_is_line_edit(line, bytes_in_line, &is_line_edit);
    if(is_line_edit) return S_OK;

    bool is_comment_line = false;

    error_code = _parse_legacy_file_is_comment_line(line, bytes_in_line, &is_comment_line);
    if(error_code) return error_code;

    if(is_comment_line) {
        (*n_comment_comment_reply)++;
    }

    return S_OK;
}

Err
_parse_legacy_file_n_comment_comment_reply_last_line(int bytes_in_line, char *line, int *n_comment_comment_reply)
{
    return _parse_legacy_file_n_comment_comment_reply_core_one_line(line, bytes_in_line, n_comment_comment_reply);
}


/*****
 * Parse Comment Comment-Reply Core
 *****/
Err
_parse_legacy_file_comment_comment_reply_core(const char *fpath, int file_size, LegacyFileInfo *legacy_file_info)
{
    Err error_code = S_OK;

    int bytes = 0;
    char buf[MAX_BUF_SIZE] = {};
    int bytes_in_line = 0;
    char line[MAX_BUF_SIZE] = {};

    int fd = open(fpath, O_RDONLY);
    int file_offset = 0;
    lseek(fd, legacy_file_info->main_content_len, SEEK_SET);
    file_size -= legacy_file_info->main_content_len;
    file_offset += legacy_file_info->main_content_len;

    enum LegacyFileStatus status = LEGACY_FILE_STATUS_COMMENT;
    int comment_idx = 0;

    time64_t current_create_milli_timestamp = legacy_file_info->create_milli_timestamp;
    while((bytes = read(fd, buf, MAX_BUF_SIZE)) > 0) {
        bytes = bytes < file_size ? bytes : file_size;
        file_size -= bytes;
        error_code = _parse_legacy_file_comment_comment_reply_core_core(buf, bytes, line, MAX_BUF_SIZE, &bytes_in_line, legacy_file_info, &comment_idx, &current_create_milli_timestamp, &status, &file_offset);
        if(error_code) break;
    }

    if(!error_code && bytes_in_line) {
        error_code = _parse_legacy_file_comment_comment_reply_core_last_line(bytes_in_line, line, legacy_file_info, &comment_idx, &current_create_milli_timestamp, &status, &file_offset);
    }

    // free
    close(fd);

    /*
    if(!error_code) {
        error_code = _parse_legacy_file_calc_comment_offset(legacy_file_info);
    }
    */

    return error_code;    
}

Err
_parse_legacy_file_calc_comment_offset(LegacyFileInfo *legacy_file_info)
{
    int current_commment_offset = legacy_file_info->main_content_len;
    for(int i = 0; i < legacy_file_info->n_comment_comment_reply; i++) {
        legacy_file_info->comment_info[i].comment_offset = current_commment_offset;
        current_commment_offset += legacy_file_info->comment_info[i].comment_len + legacy_file_info->comment_info[i].comment_reply_len;
    }

    return S_OK;
}

Err
_parse_legacy_file_comment_comment_reply_core_core(char *buf, int bytes, char *line, int line_size, int *bytes_in_line, LegacyFileInfo *legacy_file_info, int *comment_idx, time64_t *current_create_milli_timestamp, enum LegacyFileStatus *status, int *file_offset)
{
    Err error_code = S_OK;
    int bytes_in_new_line = 0;
    for(int offset_buf = 0; offset_buf < bytes; offset_buf += bytes_in_new_line) {
        error_code = get_line_from_buf(buf, offset_buf, bytes, line, *bytes_in_line, line_size, &bytes_in_new_line);
        *bytes_in_line += bytes_in_new_line;
        if(error_code) {
            error_code = S_OK;
            break;
        }

        error_code = _parse_legacy_file_comment_comment_reply_core_one_line(line, *bytes_in_line, legacy_file_info, comment_idx, current_create_milli_timestamp, status, file_offset);

        if(error_code) break;

        // reset line
        bzero(line, *bytes_in_line);
        *bytes_in_line = 0;
    }

    return error_code;
}

Err
_parse_legacy_file_comment_comment_reply_core_one_line(char *line, int bytes_in_line, LegacyFileInfo *legacy_file_info, int *comment_idx, time64_t *current_create_milli_timestamp, enum LegacyFileStatus *status, int *file_offset)
{
    Err error_code = S_OK;
    // XXX special treat for edit
    bool is_line_edit;
    error_code = _parse_legacy_file_is_line_edit(line, bytes_in_line, &is_line_edit);
    if(is_line_edit) {
        *file_offset += bytes_in_line;
        return S_OK;
    }

    // hack for state-transition in comment-reply -> comment
    switch(*status) {
    case LEGACY_FILE_STATUS_COMMENT:
        error_code = _parse_legacy_file_comment_comment_reply_core_one_line_comment(line, bytes_in_line, legacy_file_info, comment_idx, current_create_milli_timestamp, status, *file_offset);
        break;
    case LEGACY_FILE_STATUS_COMMENT_REPLY:
        error_code = _parse_legacy_file_comment_comment_reply_core_one_line_comment_reply(line, bytes_in_line, legacy_file_info, comment_idx, status, *file_offset);
        if(!error_code && *status == LEGACY_FILE_STATUS_COMMENT) {
            error_code = _parse_legacy_file_comment_comment_reply_core_one_line_comment(line, bytes_in_line, legacy_file_info, comment_idx, current_create_milli_timestamp, status, *file_offset);
        }
        break;
    default:
        break;
    }
    *file_offset += bytes_in_line;

    return error_code;
}

Err
_parse_legacy_file_comment_comment_reply_core_one_line_comment(char *line, int bytes_in_line, LegacyFileInfo *legacy_file_info, int *comment_idx, time64_t *current_create_milli_timestamp, enum LegacyFileStatus *status, int file_offset)
{
    Err error_code = S_OK;

    int tmp_comment_idx = *comment_idx;
    // comment-create-milli-timestamp
    error_code = _parse_legacy_file_comment_create_milli_timestamp(line, bytes_in_line, *current_create_milli_timestamp, &legacy_file_info->comment_info[tmp_comment_idx].comment_create_milli_timestamp);
    if(error_code) return error_code;

    // comment-poster
    error_code = _parse_legacy_file_comment_poster(line, bytes_in_line, legacy_file_info->comment_info[tmp_comment_idx].comment_poster);
    if(error_code) return error_code;

    // comment-len
    legacy_file_info->comment_info[tmp_comment_idx].comment_len = bytes_in_line;

    // comment-type
    error_code = _parse_legacy_file_comment_type(line, bytes_in_line, &legacy_file_info->comment_info[tmp_comment_idx].comment_type);
    if(error_code) return error_code;

    // comment-reply-create-milli-timestamp
    legacy_file_info->comment_info[tmp_comment_idx].comment_reply_create_milli_timestamp = legacy_file_info->comment_info[tmp_comment_idx].comment_create_milli_timestamp + 1;

    // comment-reply-poster
    strcpy(legacy_file_info->comment_info[tmp_comment_idx].comment_reply_poster, legacy_file_info->poster);

    legacy_file_info->comment_info[tmp_comment_idx].comment_offset = file_offset;

    // set status
    *status = LEGACY_FILE_STATUS_COMMENT_REPLY;
    *current_create_milli_timestamp = legacy_file_info->comment_info[tmp_comment_idx].comment_create_milli_timestamp;

    return S_OK;
}

/**
 * @brief 
 * @details The purpose here is construct the milli-timestamp that is:
 *          1. the same date in line.
 *          2. larger than current_create_milli_timestamp
 *          
 *          method:
 *          1. get the date from line.
 *          2. get the year from current_create_milli_timestamp.
 *          3. try to construct milli_timestamp.
 *          4. if new milli_timestamp is larger than current_create_milli_timestamp:
 *             year += 1
 *             reconstruct milli_timestamp
 * 
 * @param line [description]
 * @param bytes_in_line [description]
 * @param current_create_milli_timestamp [description]
 * @param create_milli_timestamp [description]
 */
Err
_parse_legacy_file_comment_create_milli_timestamp(char *line, int bytes_in_line, time64_t current_create_milli_timestamp, time64_t *create_milli_timestamp)
{
    Err error_code = S_OK;

    // comment-line-reset-karma
    bool is_valid = false;
    error_code = _parse_legacy_file_is_comment_line_reset_karma(line, bytes_in_line, &is_valid);
    if(error_code) return error_code;

    if(is_valid) {
       error_code = _parse_legacy_file_comment_create_milli_timestamp_reset_karma(line, bytes_in_line, current_create_milli_timestamp, create_milli_timestamp);
       return error_code;
    }

    // the rest (good / bad / arrow / cross_post)
    error_code = _parse_legacy_file_comment_create_milli_timestamp_good_bad_arrow_cross_post(line, bytes_in_line, current_create_milli_timestamp, create_milli_timestamp);

    return error_code;
}

Err
_parse_legacy_file_comment_create_milli_timestamp_good_bad_arrow_cross_post(char *line, int bytes_in_line, time64_t current_create_milli_timestamp, time64_t *create_milli_timestamp)
{
    Err error_code = S_OK;

    int year = 0;
    time64_t current_create_timestamp = 0;
    error_code = milli_timestamp_to_year(current_create_milli_timestamp, &year);
    if(error_code) return error_code;

    error_code = milli_timestamp_to_timestamp(current_create_milli_timestamp, &current_create_timestamp);
    if(error_code) return error_code;

    int mm = 0;
    int dd = 0;
    int HH = 0;
    int MM = 0;
    error_code = _parse_legacy_file_comment_create_milli_timestamp_get_datetime_from_line(line, bytes_in_line, &mm, &dd, &HH, &MM);
    if(error_code) return error_code;

    time64_t tmp_timestamp = 0;
    error_code = datetime_to_timestamp(year, mm, dd, HH, MM, 0, TIMEZONE_TAIPEI, &tmp_timestamp);
    if(error_code) return error_code;

    if(tmp_timestamp > current_create_timestamp) {
        *create_milli_timestamp = tmp_timestamp * 1000;
        return S_OK;
    }

    if(tmp_timestamp == current_create_timestamp) {
        *create_milli_timestamp = current_create_milli_timestamp + 1;
        return S_OK;
    }

    error_code = datetime_to_timestamp(year + 1, mm, dd, HH, MM, 0, TIMEZONE_TAIPEI, &tmp_timestamp);
    if(error_code) return error_code;

    *create_milli_timestamp = tmp_timestamp * 1000;
    return S_OK;
}

/**
 * @brief [brief description]
 * @details in format mm/dd HH:MM\r\n in the end of the line
 * 
 * @param line [description]
 * @param bytes_in_line [description]
 * @param mm [description]
 * @param dd [description]
 * @param HH [description]
 * @param MM [description]
 */
Err
_parse_legacy_file_comment_create_milli_timestamp_get_datetime_from_line(char *line, int bytes_in_line, int *mm, int *dd, int *HH, int *MM)
{
    if(bytes_in_line < LEN_COMMENT_DATETIME_IN_LINE) return S_ERR;
    char *p_line = line + bytes_in_line - 1;
    int p_line_pos = bytes_in_line - 1;
    for(; p_line_pos >= 0 && (*p_line == '\r' || *p_line == '\n'); p_line_pos--, p_line--);
    if(p_line_pos < LEN_COMMENT_DATETIME_IN_LINE) return S_ERR;
    p_line -= (LEN_COMMENT_DATETIME_IN_LINE - 1);
    p_line_pos -= (LEN_COMMENT_DATETIME_IN_LINE - 1);

    // mm
    int tmp_mm = atoi(p_line);
    if(tmp_mm < 1 || tmp_mm > 12) return S_ERR;

    // /
    p_line += 2;
    p_line_pos += 2;
    if(*p_line != '/') return S_ERR;

    // dd
    p_line++;
    p_line_pos++;
    int tmp_dd = atoi(p_line);
    if(tmp_dd < 1 || tmp_dd > 31) return S_ERR;

    // [space]
    p_line += 2;
    p_line_pos += 2;
    if(*p_line != ' ') return S_ERR;

    // HH
    p_line++;
    p_line_pos++;
    int tmp_HH = atoi(p_line);
    if(tmp_HH < 0 || tmp_HH > 23) return S_ERR;

    // :
    p_line += 2;
    p_line_pos += 2;
    if(*p_line != ':') return S_ERR;

    // MM
    p_line++;
    p_line_pos++;
    int tmp_MM = atoi(p_line);
    if(tmp_MM < 0 || tmp_MM > 59) return S_ERR;

    // \r\n
    p_line += 2;
    p_line_pos += 2;
    if(!(p_line_pos == bytes_in_line - 2 && *p_line == '\r' && *(p_line + 1) == '\n') && !(p_line_pos == bytes_in_line - 1 && *p_line == '\n')) return S_ERR;

    *mm = tmp_mm;
    *dd = tmp_dd;
    *HH = tmp_HH;
    *MM = tmp_MM;

    return S_OK;
}

Err
_parse_legacy_file_comment_poster(char *line, int bytes_in_line, char *poster)
{
    Err error_code = S_OK;

    // comment-line-cross_post
    bool is_valid = false;
    error_code = _parse_legacy_file_is_comment_line_cross_post(line, bytes_in_line, &is_valid);
    if(error_code) return error_code;

    if(is_valid) {
       error_code = _parse_legacy_file_comment_poster_cross_post(line, bytes_in_line, poster);
       return error_code;
    }

    // comment-line-reset
    error_code = _parse_legacy_file_is_comment_line_reset_karma(line, bytes_in_line, &is_valid);
    if(error_code) return error_code;

    if(is_valid) {
       error_code = _parse_legacy_file_comment_poster_reset_karma(line, bytes_in_line, poster);
       return error_code;
    }

    // the rest (good / bad / arrow)
    error_code = _parse_legacy_file_comment_poster_good_bad_arrow(line, bytes_in_line, poster);

    return error_code;
}

Err
_parse_legacy_file_comment_poster_cross_post(char *line, int bytes_in_line, char *poster)
{
    char *p_line = line + LEN_COMMENT_CROSS_POST_HEADER;
    char *p_line2 = p_line;
    for(int i = LEN_COMMENT_CROSS_POST_HEADER; *p_line2 && *p_line2 != ':' && i < bytes_in_line; i++, p_line2++);
    int len_poster = p_line2 - p_line - LEN_COMMENT_CROSS_POST_PREFIX_COLOR;
    if(len_poster < 0) return S_ERR;

    memcpy(poster, p_line, len_poster);

    return S_OK;
}

Err
_parse_legacy_file_comment_poster_reset_karma(char *line, int bytes_in_line, char *poster)
{
    char *p_line = line + 2;
    char *p_line2 = p_line;
    for(int i = 2; *p_line2 && *p_line2 != ' ' && i < bytes_in_line; i++, p_line2++);
    int len_poster = p_line2 - p_line;
    if(len_poster < 0) return S_ERR;

    memcpy(poster, p_line, len_poster);

    return S_OK;
}

Err
_parse_legacy_file_comment_poster_good_bad_arrow(char *line, int bytes_in_line, char *poster)
{
    char *p_line = line + LEN_COMMENT_HEADER;
    char *p_line2 = p_line;
    for(int i = LEN_COMMENT_HEADER; *p_line2 && *p_line2 != ':' && i < bytes_in_line; i++, p_line2++);
    int len_poster = p_line2 - p_line - LEN_COMMENT_PREFIX_COLOR;
    if(len_poster < 0) return S_ERR;

    memcpy(poster, p_line, len_poster);

    return S_OK;
}

Err
_parse_legacy_file_comment_type(char *line, int bytes_in_line, enum CommentType *comment_type)
{
    Err error = S_OK;

    bool is_valid = false;
    error = _parse_legacy_file_is_comment_line_good_bad_arrow(line, bytes_in_line, &is_valid, COMMENT_TYPE_GOOD);
    if(error) return error;
    if(is_valid) {
        *comment_type = COMMENT_TYPE_GOOD;
        return S_OK;
    }

    error = _parse_legacy_file_is_comment_line_good_bad_arrow(line, bytes_in_line, &is_valid, COMMENT_TYPE_BAD);
    if(error) return error;
    if(is_valid) {
        *comment_type = COMMENT_TYPE_BAD;
        return S_OK;
    }

    error = _parse_legacy_file_is_comment_line_good_bad_arrow(line, bytes_in_line, &is_valid, COMMENT_TYPE_ARROW);
    if(error) return error;
    if(is_valid) {
        *comment_type = COMMENT_TYPE_ARROW;
        return S_OK;
    }

    error = _parse_legacy_file_is_comment_line_cross_post(line, bytes_in_line, &is_valid);
    if(error) return error;
    if(is_valid) {
        *comment_type = COMMENT_TYPE_CROSS_POST;
        return S_OK;
    }


    error = _parse_legacy_file_is_comment_line_reset_karma(line, bytes_in_line, &is_valid);
    if(error) return error;
    if(is_valid) {
        *comment_type = COMMENT_TYPE_RESET_KARMA;
        return S_OK;
    }

    return S_OK;
}

Err
_parse_legacy_file_comment_comment_reply_core_one_line_comment_reply(char *line, int bytes_in_line, LegacyFileInfo *legacy_file_info, int *comment_idx, enum LegacyFileStatus *status, int file_offset)
{
    Err error_code = S_OK;
    bool is_comment_line = false;
    error_code = _parse_legacy_file_is_comment_line(line, bytes_in_line, &is_comment_line);
    if(error_code) return error_code;

    if(is_comment_line) {
        (*comment_idx)++;
        *status = LEGACY_FILE_STATUS_COMMENT;
        return S_OK;
    }

    legacy_file_info->comment_info[*comment_idx].comment_reply_len += bytes_in_line;
    if(!legacy_file_info->comment_info[*comment_idx].comment_reply_offset) legacy_file_info->comment_info[*comment_idx].comment_reply_offset = file_offset;

    return S_OK;
}

Err
_parse_legacy_file_comment_comment_reply_core_last_line(int bytes_in_line, char *line, LegacyFileInfo *legacy_file_info, int *comment_idx, time64_t *current_create_milli_timestamp, enum LegacyFileStatus *status, int *file_offset)
{
    return _parse_legacy_file_comment_comment_reply_core_one_line(line, bytes_in_line, legacy_file_info, comment_idx, current_create_milli_timestamp, status, file_offset);
}

/*****
 * Is Line Edit
 *****/

Err
_parse_legacy_file_is_line_edit(char *line, int bytes_in_line, bool *is_valid)
{
    if(bytes_in_line < LEN_LINE_EDIT_PREFIX) return S_ERR;

    if(strncmp(line, LINE_EDIT_PREFIX, LEN_LINE_EDIT_PREFIX)) {
        *is_valid = false;
        return S_OK;
    }

    *is_valid = true;
    return S_OK;
}

/*****
 * Is Comment line
 *****/

Err
_parse_legacy_file_is_comment_line(char *line, int bytes_in_line, bool *is_valid)
{
    Err error_code = S_OK;
    error_code = _parse_legacy_file_is_comment_line_good_bad_arrow(line, bytes_in_line, is_valid, COMMENT_TYPE_GOOD);
    if(error_code) return error_code;
    if(*is_valid) return S_OK;

    error_code = _parse_legacy_file_is_comment_line_good_bad_arrow(line, bytes_in_line, is_valid, COMMENT_TYPE_BAD);
    if(error_code) return error_code;
    if(*is_valid) return S_OK;

    error_code = _parse_legacy_file_is_comment_line_good_bad_arrow(line, bytes_in_line, is_valid, COMMENT_TYPE_ARROW);
    if(error_code) return error_code;
    if(*is_valid) return S_OK;

    error_code = _parse_legacy_file_is_comment_line_cross_post(line, bytes_in_line, is_valid);
    if(error_code) return error_code;
    if(*is_valid) return S_OK;

    error_code = _parse_legacy_file_is_comment_line_reset_karma(line, bytes_in_line, is_valid);
    if(error_code) return error_code;
    if(*is_valid) return S_OK;

    return S_OK;
}


Err
_parse_legacy_file_is_comment_line_good_bad_arrow(char *line, int bytes_in_line, bool *is_valid, enum CommentType comment_type)
{
    if(bytes_in_line < LEN_COMMENT_HEADER) {
        *is_valid = false;
        return S_OK;
    }

    static char comment_header[MIGRATE_COMMENT_BUF_SIZE] = {};
    sprintf(comment_header, "%s%s " ANSI_COLOR(33), COMMENT_TYPE_ATTR2[comment_type], COMMENT_TYPE_ATTR[comment_type]);

    if(!strncmp(line, comment_header, LEN_COMMENT_HEADER)) {
        *is_valid = true;
    }
    else {
        *is_valid = false;
    }

    return S_OK;
}

Err
_parse_legacy_file_is_comment_line_cross_post(char *line, int bytes_in_line, bool *is_valid)
{
    if(strncmp(line, COMMENT_TYPE_ATTR[COMMENT_TYPE_CROSS_POST], 2)) {
        *is_valid = false;
        return S_OK;
    }

    char *p_line = line + 3;
    while(*p_line && *p_line != '\r' && *p_line != '\n' && *p_line != ':') p_line++;
    bytes_in_line -= p_line - line;

    if(*p_line != ':') {
        *is_valid = false;
        return S_OK;
    }

    if(bytes_in_line < LEN_COMMENT_CROSS_POST_PREFIX) {
        *is_valid = false;
        return S_OK;
    }

    if(strncmp(p_line, COMMENT_CROSS_POST_PREFIX, LEN_COMMENT_CROSS_POST_PREFIX)) {
        *is_valid = false;
        return S_OK;
    }

    *is_valid = true;
    return S_OK;
}

Err
_parse_legacy_file_is_comment_line_reset_karma(char *line, int bytes_in_line, bool *is_valid)
{
    if(strncmp(line, COMMENT_TYPE_ATTR[COMMENT_TYPE_RESET_KARMA], 2)) {
        *is_valid = false;
        return S_OK;
    }

    char *p_line = line + 2;
    while(*p_line && *p_line != '\r' && *p_line != '\n' && *p_line != ' ') p_line++;
    bytes_in_line -= p_line - line;

    if(*p_line != ' ') {
        *is_valid = false;
        return S_OK;
    }

    // infix
    if(bytes_in_line < LEN_COMMENT_RESET_KARMA_INFIX) {
        *is_valid = false;
        return S_OK;
    }

    if(strncmp(p_line, COMMENT_RESET_KARMA_INFIX, LEN_COMMENT_RESET_KARMA_INFIX)) {
        *is_valid = false;
        return S_OK;
    }

    p_line += LEN_COMMENT_RESET_KARMA_INFIX;

    // mm
    int mm = atoi(p_line);
    if(mm < 1 || mm > 12) {
        *is_valid = false;
        return S_OK;
    }
    p_line += 2;

    if(*p_line != '/') {
        *is_valid = false;
        return S_OK;
    }
    p_line++;

    // dd
    int dd = atoi(p_line);
    if(dd < 1 || dd > 31) {
        *is_valid = false;
        return S_OK;
    }
    p_line += 2;

    if(*p_line != '/') {
        *is_valid = false;
        return S_OK;
    }
    p_line++;

    // yyyy
    int yyyy = atoi(p_line);
    if(yyyy < 1990) {
        *is_valid = false;
        return S_OK;
    }
    p_line += 4;

    if(*p_line != ' ') {
        *is_valid = false;
        return S_OK;
    }
    p_line++;

    // HH
    int HH = atoi(p_line);
    if(HH < 0 || HH > 23) {
        *is_valid = false;
        return S_OK;
    }
    p_line += 2;

    if(*p_line != ':') {
        *is_valid = false;
        return S_OK;
    }
    p_line++;

    // MM
    int MM = atoi(p_line);
    if(MM < 0 || MM > 59) {
        *is_valid = false;
        return S_OK;
    }
    p_line += 2;

    if(*p_line != ':') {
        *is_valid = false;
        return S_OK;
    }
    p_line++;

    // SS
    int SS = atoi(p_line);
    if(SS < 0 || SS > 59) {
        *is_valid = false;
        return S_OK;
    }
    p_line += 2;

    if(*p_line != ' ') {
        *is_valid = false;
        return S_OK;
    }

    // postfix
    if(strncmp(p_line, COMMENT_RESET_KARMA_POSTFIX, LEN_COMMENT_RESET_KARMA_POSTFIX)) {
        *is_valid = false;
        return S_OK;
    }

    *is_valid = true;
    return S_OK;
}

Err
init_legacy_file_info_comment_comment_reply(LegacyFileInfo *legacy_file_info, int n_comment_comment_reply)
{
    if(legacy_file_info->comment_info) return S_ERR;

    legacy_file_info->comment_info = malloc(sizeof(LegacyCommentInfo) * n_comment_comment_reply);    
    if(!legacy_file_info->comment_info) return S_ERR;
    bzero(legacy_file_info->comment_info, sizeof(LegacyCommentInfo) * n_comment_comment_reply);

    legacy_file_info->n_comment_comment_reply = n_comment_comment_reply;

    return S_OK;
}

Err
safe_destroy_legacy_file_info(LegacyFileInfo *legacy_file_info)
{
    safe_free((void **)&legacy_file_info->comment_info);
    legacy_file_info->n_comment_comment_reply = 0;

    return S_OK;
}