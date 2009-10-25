#ifndef _LIBBBSUTIL_H_
#define _LIBBBSUTIL_H_

#include <stdint.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
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
extern uint32_t ipstr2int(const char *ip);
extern int tobind   (const char *addr);
extern int tobindex (const char *addr, int qlen, int (*setsock)(int), int do_listen);
extern int toconnect(const char *addr);
extern int toconnectex(const char *addr, int timeout);
extern int toread   (int fd, void *buf, int len);
extern int towrite  (int fd, const void *buf, int len);
extern int torecv   (int fd, void *buf, int len, int flag);
extern int tosend   (int fd, const void *buf, int len, int flag);
extern int send_remote_fd(int tunnel, int fd);
extern int recv_remote_fd(int tunnel, const char *tunnel_path);

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
extern int  strip_blank(char *cbuf, const char *buf);
extern int  strip_ansi(char *buf, const char *str, enum STRIP_FLAG flag);
extern void strip_nonebig5(unsigned char *str, int maxlen);
extern int  invalid_pname(const char *str);
extern int  is_number(const char *p);
extern char * qp_encode (char *s, size_t slen, const char *d, const char *tocode);
extern unsigned StringHash(const char *s);
/* DBCS utilities */
extern int    DBCS_RemoveIntrEscape(unsigned char *buf, int *len);
extern int    DBCS_Status(const char *dbcstr, int pos);
extern void   DBCS_safe_trim(char *dbcstr);
extern char * DBCS_strcasestr(const char* pool, const char *ptr);
extern size_t str_iconv(
	  const char *fromcode,	/* charset of source string */
	  const char *tocode,	/* charset of destination string */
	  const char *src,	/* source string */
	  size_t srclen,		/* source string length */
	  char *dst,		/* destination string */
	  size_t dstlen);
extern void str_decode_M3(char *str);

/* time.c */
extern int is_leap_year(int year);
extern int getHoroscope(int m, int d);
extern const char* Cdate(const time4_t *clock);
extern const char* Cdatelite(const time4_t *clock);
extern const char* Cdatedate(const time4_t * clock);
extern const char * Cdate_mdHM(const time4_t * clock);
extern const char * Cdate_md(const time4_t * clock);

#ifdef TIMET64
    struct tm*	localtime4(const time4_t *);
    struct tm*	localtime4_r(const time4_t *, struct tm *);
    time4_t	time4(time4_t *);
    char*	ctime4(const time4_t *);
    char*	ctime4_r(const time4_t *, char *);
#else
    #define localtime4_r(a,b) localtime_r(a,b)
    #define localtime4(a) localtime(a)
    #define time4(a)      time(a)
    #define ctime4(a)     ctime(a)
    #define ctime4_r(a)   ctime_r(a)
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
extern int  Vector_length(const struct Vector *self);
extern void Vector_resize(struct Vector *self, const int length);
extern void Vector_add(struct Vector *self, const char *name);
extern const char* Vector_get(const struct Vector *self, const int idx);
extern int  Vector_MaxLen(const struct Vector *list, const int offset, const int count);
extern int  Vector_match(const struct Vector *src, struct Vector *dst, const int key, const int pos);
extern void Vector_sublist(const struct Vector *src, struct Vector *dst, const char *tag);
extern int  Vector_remove(struct Vector *self, const char *name);
extern int  Vector_search(const struct Vector *self, const char *name);

/* vbuf.c */
typedef struct VBUF {
    char    *buf;
    char    *buf_end;   // (buf+capacity+1)
    char    *head;      // pointer to read  (front)
    char    *tail;      // pointer to write (end)
    size_t  capacity;
} VBUF;

// speedy macros
#define vbuf_is_empty(v)    ((v)->head == (v)->tail)
#define vbuf_is_full(v)	    (!vbuf_space(v))
#define vbuf_capacity(v)    ((v)->capacity)
#define vbuf_size(v)        ((size_t)((v)->tail >= (v)->head ? (v)->tail - (v)->head : (v)->buf_end - (v)->head + (v)->tail - (v)->buf))
#define vbuf_space(v)       ((size_t)((v)->capacity - vbuf_size(v)))
#define vbuf_peek(v)	    (vbuf_is_empty(v) ? EOF : (unsigned char)(*v->head))
// buffer management
extern void vbuf_new   (VBUF *v, size_t szbuf);
extern void vbuf_delete(VBUF *v);
extern void vbuf_attach(VBUF *v, char *buf, size_t szbuf);
extern void vbuf_detach(VBUF *v);
extern void vbuf_clear (VBUF *v);
// data accessing 
extern int  vbuf_getblk(VBUF *v, void *p, size_t sz);       // get data from vbuf, return true/false
extern int  vbuf_putblk(VBUF *v, const void *p, size_t sz); // put data into vbuf, return true/false
extern int  vbuf_peekat(VBUF *v, int i);		    // peek at given index, EOF(-1) if invalid index
extern int  vbuf_pop   (VBUF *v);			    // pop one byte from vbuf, EOF(-1) if buffer empty
extern int  vbuf_add   (VBUF *v, char c);		    // append one byte into vbuf, return true/false
extern void vbuf_popn  (VBUF *v, size_t n);		    // pop (remove) n bytes from vbuf
// search and test
extern int  vbuf_strchr(VBUF *v, char c);		    // index of first location of c, otherwise EOF(-1)

// vector of C-style NULL terminated strings
extern char* vbuf_getstr(VBUF *v, char *s, size_t sz);	    // get a string from vbuf, return NULL if empty
extern int   vbuf_putstr(VBUF *v, const char *s);	    // put a string to vbuf (with NUL), return true/false
extern char *vbuf_cstr  (VBUF *v);			    // return flattern (unwrap) buffer and pad NUL, or NULL if empty.

#define VBUF_RWSZ_ALL (0)   // r/w until buffer full
#define VBUF_RWSZ_MIN (-1)  // r/w for minimal try (do not block for more)

// following APIs take VBUF_RWSZ_* in their sz parameter.
extern ssize_t vbuf_read (VBUF *v, int fd, ssize_t sz);	// read from fd to vbuf
extern ssize_t vbuf_write(VBUF *v, int fd, ssize_t sz);	// write from vbuf to fd
extern ssize_t vbuf_send (VBUF *v, int fd, ssize_t sz, int flags);
extern ssize_t vbuf_recv (VBUF *v, int fd, ssize_t sz, int flags);

// write from vbuf to writer
extern ssize_t vbuf_general_write(VBUF *v, ssize_t sz, void *ctx, 
	ssize_t (writer)(struct iovec[2], void *ctx));
        // ssize_t (writer)(const void *p[2], size_t len[2], void *ctx)); 
// read from reader to vbuf
extern ssize_t vbuf_general_read (VBUF *v, ssize_t sz, void *ctx, 
	ssize_t (reader)(struct iovec[2], void *ctx));
        // ssize_t (reader)(void *p[2], size_t len[2], void *ctx));

/* telnet.c */
struct TelnetCallback {
    void (*write_data)		(void *write_arg, int fd, const void *buf, size_t nbytes);
    void (*term_resize)		(void *resize_arg,int w, int h);
    void (*update_client_code)	(void *cc_arg,    unsigned char seq);
    void (*send_ayt)		(void *ayt_arg,   int fd);
    void (*ttype)		(void *ttype_arg, char *ttype, int ttype_len);
};

#define TELNET_IAC_MAXLEN (16)

struct TelnetCtx {
    int fd;		// should be blocking fd
    unsigned char iac_state;
    
    unsigned char iac_quote;
    unsigned char iac_opt_req;

    unsigned char iac_buf[TELNET_IAC_MAXLEN];
    unsigned int  iac_buflen;

    const struct TelnetCallback *callback;

    // callback parameters
    void *write_arg;	// write_data
    void *resize_arg;	// term_resize
    void *cc_arg;	// update_client_code
    void *ayt_arg;	// send_ayt
    void *ttype_arg;	// term_type (ttype)
};
typedef struct TelnetCtx TelnetCtx;

extern TelnetCtx *telnet_create_context(void);
extern void       telnet_free_context  (TelnetCtx *ctx);

extern void telnet_ctx_init          (TelnetCtx *ctx, const struct TelnetCallback *callback, int fd);
extern void telnet_ctx_send_init_cmds(TelnetCtx *ctx);

extern void telnet_ctx_set_cc_arg    (TelnetCtx *ctx, void *arg);
extern void telnet_ctx_set_write_arg (TelnetCtx *ctx, void *arg);
extern void telnet_ctx_set_resize_arg(TelnetCtx *ctx, void *arg);
extern void telnet_ctx_set_ayt_arg   (TelnetCtx *ctx, void *arg);
extern void telnet_ctx_set_ttype_arg (TelnetCtx *ctx, void *arg);

extern ssize_t telnet_process        (TelnetCtx *ctx, unsigned char *buf, ssize_t size);

#endif
