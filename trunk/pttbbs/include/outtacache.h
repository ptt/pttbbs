#ifndef INCLUDE_OUTTACACHE_H
#define INCLUDE_OUTTACACHE_H

#define CACHE_BUFSIZE (200*1024)
#define OC_HEADERLEN  (sizeof(OCkey_t) + sizeof(int))
#define OC_KEYLEN     (sizeof(OCkey_t))
#define OC_pidadd     10000000
#define OC_msto  5111
#define OC_mtos  5112

typedef struct {
    pid_t   pid;
    char    cacheid;
} OCkey_t;

typedef struct {
    OCkey_t key;
    int     length;
    char    buf[CACHE_BUFSIZE];
} OCbuf_t;


typedef struct {
    time_t  mtime;
    OCbuf_t data;
} OCstore_t;
    
#endif
