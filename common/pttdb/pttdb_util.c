#include "cmpttdb/pttdb_util.h"

Err
safe_free(void **a)
{
    if(!(*a)) return S_OK;
    
    free(*a);
    *a = NULL;
    return S_OK;
}

/**
 * @brief Try to get a line (ending with \n) from buffer.
 * @details [long description]
 *
 * @param p_buf Starting point of the buffer.
 * @param offset_buf Current offset of the p_buf in the whole buffer.
 * @param bytes Total bytes of the buffer.
 * @param p_line Starting point of the line.
 * @param offset_line Offset of the line.
 * @param bytes_in_new_line To be obtained bytes in new extracted line.
 * @return Error
 */
Err
get_line_from_buf(char *buf, int offset_buf, int buf_size, char *p_line, int offset_line, int line_size, int *bytes_in_new_line)
{

    // check the end of buf
    if (offset_buf >= buf_size) {
        *bytes_in_new_line = 0;

        return S_ERR;
    }

    // init p_buf offset
    char *p_buf = buf + offset_buf;
    p_line += offset_line;

    // check \n in buf.
    int max_new_lines = line_size - offset_line;
    int iter_bytes = (buf_size - offset_buf <= max_new_lines) ? (buf_size - offset_buf) : max_new_lines;

    for (int i = 0; i < iter_bytes; i++) {
        if (*p_buf == '\n') {
            *p_line = '\n';
            *bytes_in_new_line = i + 1;
 
            return S_OK;
        }

        *p_line++ = *p_buf++;
    }

    // last char
    *bytes_in_new_line = iter_bytes;

    // XXX special case for all block as a continuous string. Although it's not end yet, it forms a block.
    if(offset_line + *bytes_in_new_line == line_size) return S_OK;

    return S_ERR;
}

/**
 * @brief Count lines in the content (based on '\n')
 * @details Count lines in the content (based on '\n')
 * 
 * @param content [description]
 * @param len [description]
 * @param n_line n_line
 */
Err
pttdb_count_lines(char *content, int len, int *n_line)
{
    int tmp_n_line = 0;
    char *p_content = content;
    for(int i = 0; i < len; i++, p_content++) {
        if(*p_content == '\n') {
            tmp_n_line++;
        }
    }
    *n_line = tmp_n_line;

    return S_OK;
}
