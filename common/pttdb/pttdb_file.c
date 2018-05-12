#include "cmpttdb/pttdb_file.h"
#include "cmpttdb/pttdb_file_private.h"

Err
init_pttdb_file()
{
    char dir_prefix[MAX_FILENAME_SIZE] = {};
    Err error_code = pttdb_file_get_dir_prefix_name(dir_prefix);
    if(error_code) return error_code;

    int ret = Mkdir(dir_prefix);
    if(ret < 0 && errno != EEXIST) return S_ERR;

    return S_OK;
}

Err
destroy_pttdb_file()
{
    char dir_prefix[MAX_FILENAME_SIZE] = {};
    Err error_code = pttdb_file_get_dir_prefix_name(dir_prefix);
    if (error_code) return error_code;

    Rmdir(dir_prefix, NULL);    

    return S_OK;
}


Err
pttdb_file_get_dir_prefix_name(char *dir_prefix)
{
    setuserfile(dir_prefix, PTTDB_FILE_PREFIX_DIR);

    return S_OK;
}

Err
pttdb_file_get_main_dir_prefix_name(UUID main_id, char *dirname)
{
    Err error_code = pttdb_file_get_dir_prefix_name(dirname);
    if(error_code) return error_code;

    char *disp_uuid = display_urlsafe_uuid(main_id);
    char *p_dirname = dirname + strlen(dirname);
    sprintf(p_dirname, "/m%c", disp_uuid[0]);
    free(disp_uuid);

    return S_OK;
}

Err
pttdb_file_attach_main_dir(UUID main_id, char *dirname)
{
    char *disp_uuid = display_urlsafe_uuid(main_id);
    char *p_dirname = dirname + strlen(dirname);
    sprintf(p_dirname, "/M%s", disp_uuid);
    free(disp_uuid);

    return S_OK;
}

Err
pttdb_file_get_main_dir_name(UUID main_id, char *dirname)
{
    Err error_code = pttdb_file_get_main_dir_prefix_name(main_id, dirname);
    if(error_code) return error_code;

    error_code = pttdb_file_attach_main_dir(main_id, dirname);

    return error_code;
}

Err
pttdb_file_get_data(UUID main_id, enum PttDBContentType content_type, UUID content_id, int block_id, int file_id, char **buf, int *len)
{
    char filename[MAX_FILENAME_SIZE] = {};
    char dir_prefix[MAX_FILENAME_SIZE] = {};

    Err error_code = pttdb_file_get_main_dir_name(main_id, dir_prefix);
    if(error_code) return error_code;

    char *disp_uuid = display_urlsafe_uuid(content_id);
    sprintf(filename, "%s/T%d/U%s/B%d/F%d", dir_prefix, content_type, disp_uuid, block_id, file_id);
    free(disp_uuid);

    int tmp_len = 0;
    int fd = open(filename, O_RDONLY);

    int max_buf_size = MAX_BUF_SIZE;
    int bytes = 0;
    char *p_buf = malloc(max_buf_size);
    while((bytes = read(fd, p_buf + tmp_len, MAX_BUF_SIZE)) > 0) {
        p_buf = realloc(p_buf, max_buf_size + MAX_BUF_SIZE);
        tmp_len += bytes;
        max_buf_size += MAX_BUF_SIZE;
    }
    close(fd);

    p_buf[tmp_len] = 0;
    *buf = p_buf;
    *len = tmp_len;

    return S_OK;
}

Err
pttdb_file_save_data(UUID main_id, enum PttDBContentType content_type, UUID content_id, int block_id, int file_id, char *buf, int len)
{
    char filename[MAX_FILENAME_SIZE] = {};
    char *p_filename = filename;

    Err error_code = pttdb_file_get_main_dir_prefix_name(main_id, p_filename);
    if(error_code) return error_code;
    int ret = Mkdir(filename);
    if(ret < 0 && errno != EEXIST) error_code = S_ERR;
    if(error_code) return error_code;

    // no need to do p_filename because attach_main_dir take care of it.
    error_code = pttdb_file_attach_main_dir(main_id, p_filename);
    if(error_code) return error_code;
    ret = Mkdir(filename);    
    if(ret < 0 && errno != EEXIST) error_code = S_ERR;
    if(error_code) return error_code;

    while(*p_filename) p_filename++;

    sprintf(p_filename, "/T%d", content_type);
    ret = Mkdir(filename);
    if(ret < 0 && errno != EEXIST) error_code = S_ERR;
    if(error_code) return error_code;

    while(*p_filename) p_filename++;

    char *disp_uuid = display_urlsafe_uuid(content_id);
    sprintf(p_filename, "/U%s", disp_uuid);
    free(disp_uuid);
    ret = Mkdir(filename);
    if(ret < 0 && errno != EEXIST) error_code = S_ERR;
    if(error_code) return error_code;

    while(*p_filename) p_filename++;

    sprintf(p_filename, "/B%d", block_id);
    ret = Mkdir(filename);
    if(ret < 0 && errno != EEXIST) error_code = S_ERR;
    if(error_code) return error_code;

    while(*p_filename) p_filename++;

    sprintf(p_filename, "/F%d", file_id);

    int fd = OpenCreate(filename, O_WRONLY);
    write(fd, buf, len);
    close(fd);

    return error_code;
}
