#include "cmpttlib/pttlib_util.h"

Err
safe_strcat(char **buf, int *max_buf_size, int alloc_size, int *len_buf, char *new_buf, int len_new_buf)
{
    int tmp_len_buf = *len_buf;
    int tmp_max_buf_size = *max_buf_size;
    int factor = 0;
    if(tmp_len_buf + len_new_buf >= tmp_max_buf_size) {
        factor = ((tmp_len_buf + len_new_buf - tmp_max_buf_size) / alloc_size) + 1;
        tmp_max_buf_size += factor * alloc_size;
        *max_buf_size = tmp_max_buf_size;
        *buf = realloc(*buf, tmp_max_buf_size);
    }

    char *p_buf = *buf + tmp_len_buf;
    memcpy(p_buf, new_buf, len_new_buf);
    p_buf[len_new_buf] = 0;
    *len_buf += len_new_buf;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details ref: https://stackoverflow.com/questions/4204666/how-to-list-files-in-a-directory-in-a-c-program
 * 
 * @param dir [description]
 */
int
Rmdir(char *dir, char *dir_prefix)
{
    DIR *d = NULL;
    struct dirent *d2;

    char new_dir_prefix[MAX_FILENAME_SIZE] = {};
    char filename[MAX_FILENAME_SIZE] = {};

    dir_prefix ? sprintf(new_dir_prefix, "%s/%s", dir_prefix, dir) : sprintf(new_dir_prefix, "%s", dir);

    d = opendir(new_dir_prefix);
    if(!d) return errno;

    int ret = 0;

    while((d2 = readdir(d)) != NULL) {
        switch(d2->d_type) {
        case DT_DIR:
            if(strstr(d2->d_name, ".") == NULL) {
                ret = Rmdir(d2->d_name, new_dir_prefix);
            }
            break;
        default:
            sprintf(filename, "%s/%s", new_dir_prefix, d2->d_name);
            unlink(filename);
            break;
        }
    }
    closedir(d);

    rmdir(new_dir_prefix);

    return 0;
}
