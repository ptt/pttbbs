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
 * 將 string append 到檔案 file 後端
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
 * 傳回檔案 file 中是否有 string 這個字串。
 */
int file_exist_record(const char *file, const char *string)
{
    FILE           *fp;
    char            buf[STRLEN], *ptr;

    if ((fp = fopen(file, "r")) == NULL)
	return 0;

    while (fgets(buf, STRLEN, fp)) {
	if ((ptr = strtok(buf, str_space)) && !strcasecmp(ptr, string)) {
	    fclose(fp);
	    return 1;
	}
    }
    fclose(fp);
    return 0;
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
