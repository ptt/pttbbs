#include <stdio.h>
#include <stdlib.h> // random
#include <sys/file.h> // flock
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <strings.h>
#include <dirent.h>
#include <string.h>
#include <sys/wait.h>

#include "libbbsutil.h"


/* ----------------------------------------------------- */
/* 檔案檢查函數：檔案、目錄、屬於                        */
/* ----------------------------------------------------- */

/**
 * 傳回 fname 的檔案大小
 * @param fname
 */
off_t
dashs(const char *fname)
{
    struct stat     st;

    if (!stat(fname, &st))
	return st.st_size;
    else
	return -1;
}

/**
 * 傳回 fname 的 mtime
 * @param fname
 */
time4_t
dasht(const char *fname)
{
    struct stat     st;

    if (!stat(fname, &st))
	return st.st_mtime;
    else
	return -1;
}

/**
 * 傳回 fname 的 ctime
 * @param fname
 */
time4_t
dashc(const char *fname)
{
    struct stat     st;

    if (!stat(fname, &st))
	return st.st_ctime;
    else
	return -1;
}

/**
 * 傳回 fname 是否為 symbolic link
 * @param fname
 */
int
dashl(const char *fname)
{
    struct stat     st;

    return (lstat(fname, &st) == 0 && S_ISLNK(st.st_mode));
}

/**
 * 傳回 fname 是否為一般的檔案
 * @param fname
 */
int
dashf(const char *fname)
{
    struct stat     st;

    return (stat(fname, &st) == 0 && S_ISREG(st.st_mode));
}

/**
 * 傳回 fname 是否為目錄
 * @param fname
 */
int
dashd(const char *fname)
{
    struct stat     st;

    return (stat(fname, &st) == 0 && S_ISDIR(st.st_mode));
}

/* ----------------------------------------------------- */
/* 檔案操作函數：複製、搬移、附加                        */
/* ----------------------------------------------------- */

#define BUFFER_SIZE	8192
int copy_file_to_file(const char *src, const char *dst)
{
    char buf[BUFFER_SIZE];
    int fdr, fdw, len;

    if ((fdr = open(src, O_RDONLY)) < 0)
	return -1;

    if ((fdw = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) {
	close(fdr);
	return -1;
    }

    while (1) {
	len = read(fdr, buf, sizeof(buf));
	if (len <= 0)
	    break;
	write(fdw, buf, len);
	if (len < BUFFER_SIZE)
	    break;
    }

    close(fdr);
    close(fdw);
    return 0;
}
#undef BUFFER_SIZE

int copy_file_to_dir(const char *src, const char *dst)
{
    char buf[PATH_MAX];
    char *slash;
    if ((slash = rindex(src, '/')) == NULL)
	snprintf(buf, sizeof(buf), "%s/%s", dst, src);
    else
	snprintf(buf, sizeof(buf), "%s/%s", dst, slash);
    return copy_file_to_file(src, buf);
}

int copy_dir_to_dir(const char *src, const char *dst)
{
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char buf[PATH_MAX], buf2[PATH_MAX];

    if (stat(dst, &st) < 0)
	if (mkdir(dst, 0700) < 0)
	    return -1;

    if ((dir = opendir(src)) == NULL)
	return -1;

    while ((entry = readdir(dir)) != NULL) {
	if (strcmp(entry->d_name, ".") == 0 ||
	    strcmp(entry->d_name, "..") == 0)
	    continue;
	snprintf(buf, sizeof(buf), "%s/%s", src, entry->d_name);
	snprintf(buf2, sizeof(buf2), "%s/%s", dst, entry->d_name);
	if (stat(buf, &st) < 0)
	    continue;
	if (S_ISDIR(st.st_mode))
	    mkdir(buf2, 0700);
	copy_file(buf, buf2);
    }

    closedir(dir);
    return 0;
}

/**
 * copy src to dst (recursively)
 * @param src and dst are file or dir
 * @return -1 if failed
 */
int copy_file(const char *src, const char *dst)
{
    struct stat st;

    if (stat(dst, &st) == 0 && S_ISDIR(st.st_mode)) {
	if (stat(src, &st) < 0)
	    return -1;
	
    	if (S_ISDIR(st.st_mode))
	    return copy_dir_to_dir(src, dst);
	else if (S_ISREG(st.st_mode))
	    return copy_file_to_dir(src, dst);
	return -1;
    }
    else if (stat(src, &st) == 0 && S_ISDIR(st.st_mode))
	return copy_dir_to_dir(src, dst);
    return copy_file_to_file(src, dst);
}

int
Rename(const char *src, const char *dst)
{
    if (rename(src, dst) == 0)
	return 0;
    if (!strchr(src, ';') && !strchr(dst, ';'))
    {
	pid_t pid = fork();
	if (pid == 0)
	    execl("/bin/mv", "mv", "-f", src, dst, (char *)NULL);
	else if (pid > 0)
	{
	    int status = -1;
	    waitpid(pid, &status, 0);
	    return WEXITSTATUS(status) == 0 ? 0 : -1;
	}
	else
	    return -1;
    }
    return -1;
}

int
Copy(const char *src, const char *dst)
{
    int fi, fo, bytes;
    char buf[8192];
    fi=open(src, O_RDONLY);
    if(fi<0) return -1;
    fo=open(dst, O_WRONLY | O_TRUNC | O_CREAT, 0600);
    if(fo<0) {close(fi); return -1;}
    while((bytes=read(fi, buf, sizeof(buf)))>0)
         write(fo, buf, bytes);
    close(fo);
    close(fi);
    return 0;  
}

int
CopyN(const char *src, const char *dst, int n)
{
    int fi, fo, bytes;
    char buf[8192];

    fi=open(src, O_RDONLY);
    if(fi<0) return -1;

    fo=open(dst, O_WRONLY | O_TRUNC | O_CREAT, 0600);
    if(fo<0) {close(fi); return -1;}

    while(n > 0 && (bytes=read(fi, buf, sizeof(buf)))>0)
    {
	 n -= bytes;
	 if (n < 0)
	     bytes += n;
         write(fo, buf, bytes);
    }
    close(fo);
    close(fi);
    return 0;  
}

/* append data from tail of src (starting point=off) to dst */
int
AppendTail(const char *src, const char *dst, int off)
{
    int fi, fo, bytes;
    char buf[8192];

    fi=open(src, O_RDONLY);
    if(fi<0) return -1;

    fo=open(dst, O_WRONLY | O_APPEND | O_CREAT, 0600);
    if(fo<0) {close(fi); return -1;}
    // flock(dst, LOCK_SH);
    
    if(off > 0)
	lseek(fi, (off_t)off, SEEK_SET);

    while((bytes=read(fi, buf, sizeof(buf)))>0)
    {
         write(fo, buf, bytes);
    }
    // flock(dst, LOCK_UN);
    close(fo);
    close(fi);
    return 0;  
}

/**
 * @param src	file
 * @param dst	file
 * @return 0	if success
 */
int
Link(const char *src, const char *dst)
{
    if (symlink(src, dst) == 0)
       return 0;

    return Copy(src, dst);
}

/* ----------------------------------------------------- */
/* 檔案內容處理函數：以「行」為單位                      */
/* ----------------------------------------------------- */

#define LINEBUFSZ (PATH_MAX)
#define STR_SPACE " \t\n\r"


/**
 * 傳回 file 檔的行數
 * @param file
 */
int file_count_line(const char *file)
{
    FILE           *fp;
    int             count = 0;
    char            buf[LINEBUFSZ];

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
    char            buf[LINEBUFSZ], *ptr;
    int i = 0;

    if ((fp = fopen(file, "r")) == NULL)
	return 0;

    while (fgets(buf, LINEBUFSZ, fp)) {
	char *strtok_pos;
	i++;
	if ((ptr = strtok_r(buf, STR_SPACE, &strtok_pos)) && !strcasecmp(ptr, key)) {
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
    char fnew[PATH_MAX];
    char buf[LINEBUFSZ + 1];
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
	    size_t klen = strcspn(buf, STR_SPACE);
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
