/* $Id$ */

#include "bbs.h"

/**
 * file.c 是針對以"行"為單位的檔案所定義的一些 operation。
 */

/**
 * 傳回 file 檔的行數
 * @param file
 */
int file_count_line(const char *file)
{
    FILE           *fp;
    int             count = 0;
    char            buf[200];

    if ((fp = fopen(file, "r"))) {
	while (fgets(buf, sizeof(buf), fp)) {
	    if (strchr(buf, '\n') == NULL)
		continue;
	    count++;
	}
	fclose(fp);
    }
    return count;
}

/**
 * 將 string append 到檔案 file 後端 (不加換行)
 * @param file 要被 append 的檔
 * @param string
 * @return 成功傳回 0，失敗傳回 -1。
 */
int file_append_line(const char *file, const char *string)
{
    FILE *fp;
    if ((fp = fopen(file, "a")) == NULL)
	return -1;
    flock(fileno(fp), LOCK_EX);
    fputs(string, fp);
    flock(fileno(fp), LOCK_UN);
    fclose(fp);
    return 0;
}

/**
 * 將 "$key\n" append 到檔案 file 後端
 * @param file 要被 append 的檔
 * @param key 沒有換行的字串
 * @return 成功傳回 0，失敗傳回 -1。
 */
int file_append_record(const char *file, const char *key)
{
    FILE *fp;
    if (!key || !*key) return -1;
    if ((fp = fopen(file, "a")) == NULL)
	return -1;
    flock(fileno(fp), LOCK_EX);
    fputs(key, fp);
    fputs("\n", fp);
    flock(fileno(fp), LOCK_UN);
    fclose(fp);
    return 0;
}

/**
 * 傳回檔案 file 中 key 所在行數
 */
int file_find_record(const char *file, const char *key)
{
    FILE           *fp;
    char            buf[STRLEN], *ptr;
    int i = 0;

    if ((fp = fopen(file, "r")) == NULL)
	return 0;

    while (fgets(buf, STRLEN, fp)) {
	char *strtok_pos;
	i++;
	if ((ptr = strtok_r(buf, str_space, &strtok_pos)) && !strcasecmp(ptr, key)) {
	    fclose(fp);
	    return i;
	}
    }
    fclose(fp);
    return 0;
}

/**
 * 傳回檔案 file 中是否有 key
 */
int file_exist_record(const char *file, const char *key)
{
    return file_find_record(file, key) > 0 ? 1 : 0;
}

/**
 * 刪除檔案 file 中以 string 開頭的行
 * @param file 要處理的檔案
 * @param string 尋找的 key name
 * @param case_sensitive 是否要處理大小寫
 * @return 成功傳回 0，失敗傳回 -1。
 */
int
file_delete_record(const char *file, const char *string, int case_sensitive)
{
    // TODO nfp 用 tmpfile() 比較好？ 不過 Rename 會變慢...
    FILE *fp = NULL, *nfp = NULL;
    char fnew[PATHLEN];
    char buf[STRLEN + 1];
    int ret = -1, i = 0;
    const size_t toklen = strlen(string);

    if (!toklen)
	return 0;

    do {
	snprintf(fnew, sizeof(fnew), "%s.%3.3X", file, (unsigned int)(random() & 0xFFF));
	if (access(fnew, 0) != 0)
	    break;
    } while (i++ < 10); // max tries = 10

    if (access(fnew, 0) == 0) return -1;    // cannot create temp file.

    i = 0;
    if ((fp = fopen(file, "r")) && (nfp = fopen(fnew, "w"))) {
	while (fgets(buf, sizeof(buf), fp))
	{
	    size_t klen = strcspn(buf, str_space);
	    if (toklen == klen)
	    {
		if (((case_sensitive && strncmp(buf, string, toklen) == 0) ||
		    (!case_sensitive && strncasecmp(buf, string, toklen) == 0)))
		{
		    // found line. skip it.
		    i++;
		    continue;
		}
	    }
	    // other wise, keep the line.
	    fputs(buf, nfp);
	}
	fclose(nfp); nfp = NULL;
	if (i > 0)
	{
	    if(Rename(fnew, file) < 0)
		ret = -1;
	    else
		ret = 0;
	} else {
	    unlink(fnew);
	    ret = 0;
	}
    }
    if(fp)
	fclose(fp);
    if(nfp)
	fclose(nfp);
    return ret;
}

/**
 * 對每一筆 record 做 func 這件事。
 * @param file
 * @param func 處理每筆 record 的 handler，為一 function pointer。
 *        第一個參數是檔案中的一行，第二個參數為 info。
 * @param info 一個額外的參數。
 */
int file_foreach_entry(const char *file, int (*func)(char *, int), int info)
{
    char line[80];
    FILE *fp;

    if ((fp = fopen(file, "r")) == NULL)
	return -1;

    while (fgets(line, sizeof(line), fp)) {
	(*func)(line, info);
    }

    fclose(fp);
    return 0;
}
