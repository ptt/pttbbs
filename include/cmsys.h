#ifndef _LIBBBSUTIL_H_
#define _LIBBBSUTIL_H_

#include <stdint.h>
#include <sys/types.h>

#include "osdep.h"
#include "config.h" // XXX for TIMET64, but config.h has too much thing I don't want ...

#ifdef __GNUC__
#define GCC_CHECK_FORMAT(a,b) __attribute__ ((format (printf, a, b)))
#else
#define GCC_CHECK_FORMAT(a,b)
#endif


// flags used by strip_ansi
enum STRIP_FLAG {
    STRIP_ALL = 0, 
    ONLY_COLOR,	    // allow only colors (ESC [ .. m)
    NO_RELOAD	    // allow all known (color+move)
};

enum LOG_FLAG {
    LOG_CREAT = 1,
};

/* DBCS aware modes */
enum _DBCS_STATUS {
    DBCS_ASCII,
    DBCS_LEADING,
    DBCS_TRAILING,
};

#ifdef TIMET64
typedef int32_t time4_t;
#else
typedef time_t time4_t;
#endif

/* crypt.c */
char *fcrypt(const char *key, const char *salt);

/* file.c */
extern off_t   dashs(const char *fname);
extern time4_t dasht(const char *fname);
extern time4_t dashc(const char *fname);
extern int dashl(const char *fname);
extern int dashf(const char *fname);
extern int dashd(const char *fname);
extern int copy_file_to_file(const char *src, const char *dst);
extern int copy_file_to_dir(const char *src, const char *dst);
extern int copy_dir_to_dir(const char *src, const char *dst);
extern int copy_file(const char *src, const char *dst);
extern int Rename(const char *src, const char *dst);
extern int Copy(const char *src, const char *dst);
extern int CopyN(const char *src, const char *dst, int n);
extern int AppendTail(const char *src, const char *dst, int off);
extern int Link(const char *src, const char *dst);
extern int HardLink(const char *src, const char *dst);
extern int file_count_line(const char *file);
extern int file_append_line(const char *file, const char *string); // does not append "\n"
extern int file_append_record(const char *file, const char *key);  // will append "\n"
extern int file_exist_record(const char *file, const char *key);
extern int file_find_record(const char *file, const char *key);
extern int file_delete_record(const char *file, const char *key, int case_sensitive);


/* lock.c */
extern void PttLock(int fd, int start, int size, int mode);

/* net.c */
extern unsigned int ipstr2int(const char *ip);
extern int tobind(const char *addr);
extern int toconnect(const char *addr);
extern int toread(int fd, void *buf, int len);
extern int towrite(int fd, const void *buf, int len);

/* sort.c */
extern int cmp_int(const void *a, const void *b);
extern int cmp_int_desc(const void * a, const void * b);
extern int *intbsearch(int key, const int *base0, int nmemb);
extern unsigned int *uintbsearch(const unsigned int key, const unsigned int *base0, const int nmemb);

/* string.h */
extern void str_lower(char *t, const char *s);
extern void trim(char *buf);
extern void chomp(char *src);
extern int  strlen_noansi(const char *s);
extern int  strat_ansi(int count, const char *s);
extern int  strip_blank(char *cbuf, char *buf);
extern int  strip_ansi(char *buf, const char *str, enum STRIP_FLAG flag);
extern void strip_nonebig5(unsigned char *str, int maxlen);
extern int  DBCS_RemoveIntrEscape(unsigned char *buf, int *len);
extern int  DBCS_Status(const char *dbcstr, int pos);
extern int  DBCS_strcasestr(const char* pool, const char *ptr);
extern int  invalid_pname(const char *str);
extern int  is_number(const char *p);
extern char * qp_encode (char *s, size_t slen, const char *d, const char *tocode);
extern unsigned StringHash(const char *s);

/* time.c */
extern int is_leap_year(int year);
extern int getHoroscope(int m, int d);
extern char* Cdate(const time4_t *clock);
extern char* Cdatelite(const time4_t *clock);
extern char* Cdatedate(const time4_t * clock);
extern char* my_ctime(const time4_t * t, char *ans, int len);
#ifdef TIMET64
    struct tm *localtime4(const time4_t *);
    time4_t time4(time4_t *);
    char *ctime4(const time4_t *);
#else
    #define localtime4(a) localtime(a)
    #define time4(a)      time(a)
    #define ctime4(a)     ctime(a)
#endif

extern int log_filef(const char *fn, int flag, const char *fmt,...) GCC_CHECK_FORMAT(3,4);
extern int log_file(const char *fn, int flag, const char *msg);

#endif
