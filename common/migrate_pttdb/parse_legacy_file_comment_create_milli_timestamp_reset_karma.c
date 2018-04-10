#define _XOPEN_SOURCE
#include "cmmigrate_pttdb/parse_legacy_file_comment_create_milli_timestamp_reset_karma.h"

Err
_parse_legacy_file_comment_create_milli_timestamp_reset_karma(char *line, int bytes_in_line, time64_t current_create_milli_timestamp, time64_t *create_milli_timestamp)
{
    char *p_line = line;

    // to COMMENT_RESET_KARMA_INFIX
    for(int i = 0; i < bytes_in_line && *p_line && *p_line != ' '; i++, p_line++);        
    p_line += LEN_COMMENT_RESET_KARMA_INFIX;
    // datetime

    struct tm the_tm;
    char *ret = strptime(p_line, "%m/%d/%Y %H:%M:%S", &the_tm);
    if(ret == NULL) return S_ERR;

    time64_t timestamp = mktime(&the_tm);
    time64_t tmp_milli_timestamp = timestamp * 1000;
    if(tmp_milli_timestamp < current_create_milli_timestamp) return S_ERR;
    if(tmp_milli_timestamp == current_create_milli_timestamp) tmp_milli_timestamp++;

    *create_milli_timestamp = tmp_milli_timestamp;

    return S_OK;
}
