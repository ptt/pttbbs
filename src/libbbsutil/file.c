#include <stdio.h>
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
	    waitpid(pid, NULL, 0);
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
    
    if(off > 0)
	lseek(fi, (off_t)off, SEEK_SET);

    while((bytes=read(fi, buf, sizeof(buf)))>0)
    {
         write(fo, buf, bytes);
    }
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

