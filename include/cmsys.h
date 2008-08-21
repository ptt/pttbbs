#ifndef _LIBBBSUTIL_H_
#define _LIBBBSUTIL_H_

#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>
#include <time.h>

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

/* daemon.c */
extern int daemonize(const char * pidfile, const char * logfile);

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
extern int  invalid_pname(const char *str);
extern int  is_number(const char *p);
extern char * qp_encode (char *s, size_t slen, const char *d, const char *tocode);
extern unsigned StringHash(const char *s);
/* DBCS utilities */
extern int	    DBCS_RemoveIntrEscape(unsigned char *buf, int *len);
extern int	    DBCS_Status(const char *dbcstr, int pos);
extern const char*  DBCS_strcasestr(const char* pool, const char *ptr);

/* time.c */
extern int is_leap_year(int year);
extern int getHoroscope(int m, int d);
extern const char* Cdate(const time4_t *clock);
extern const char* Cdatelite(const time4_t *clock);
extern const char* Cdatedate(const time4_t * clock);
extern const char * Cdate_mdHM(const time4_t * clock);
extern const char * Cdate_md(const time4_t * clock);
extern const char* my_ctime(const time4_t * t, char *ans, int len);
extern struct tm localtime4r(const time4_t *t);

#ifdef TIMET64
    struct tm*	localtime4(const time4_t *);
    struct tm*	localtime4_r(const time4_t *, struct tm *);
    time4_t	time4(time4_t *);
    char*	ctime4(const time4_t *);
#else
    #define localtime4_r(a,b) localtime_r(a,b)
    #define localtime4(a) localtime(a)
    #define time4(a)      time(a)
    #define ctime4(a)     ctime(a)
#endif

extern int log_filef(const char *fn, int flag, const char *fmt,...) GCC_CHECK_FORMAT(3,4);
extern int log_file(const char *fn, int flag, const char *msg);

/* record.c */
int get_num_records(const char *fpath, size_t size);
int get_records_keep(const char *fpath, void *rptr, size_t size, int id, size_t number, int *fd);
int get_records(const char *fpath, void *rptr, size_t size, int id, size_t number);
#define get_record(fpath, rptr, size, id) get_records(fpath, rptr, size, id, 1)
#define get_record_keep(fpath, rptr, size, id, fd) get_records_keep(fpath, rptr, size, id, 1, fd)
int substitute_record(const char *fpath, const void *rptr, size_t size, int id);
int append_record(const char *fpath, const void *record, size_t size);
int delete_records(const char *fpath, size_t size, int id, size_t num);
#define delete_record(fpath, size, id) delete_records(fpath, size, id, 1)
int apply_record(const char *fpath, int (*fptr) (void *item, void *optarg), size_t size, void *arg);

/* vector.c */
struct Vector {
    int size;
    int length;
    int capacity;
    char *base;
    bool constant;
};

extern void Vector_init(struct Vector *self, const int size);
extern void Vector_init_const(struct Vector *self, char * base, const int length, const int size);
extern void Vector_delete(struct Vector *self);
extern void Vector_clear(struct Vector *self, const int size);
extern int Vector_length(const struct Vector *self);
extern void Vector_resize(struct Vector *self, const int length);
extern void Vector_add(struct Vector *self, const char *name);
extern const char* Vector_get(const struct Vector *self, const int idx);
extern int Vector_MaxLen(const struct Vector *list, const int offset, const int count);
extern int Vector_match(const struct Vector *src, struct Vector *dst, const int key, const int pos);
extern void Vector_sublist(const struct Vector *src, struct Vector *dst, const char *tag);
extern int Vector_remove(struct Vector *self, const char *name);
extern int Vector_search(const struct Vector *self, const char *name);

#endif
