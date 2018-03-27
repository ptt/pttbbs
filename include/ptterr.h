/* $Id$ */
#ifndef PTTERR_H
#define PTTERR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef char Err;

#define S_OK 0
#define S_ERR -1
#define S_ERR_MALLOC -2
#define S_ERR_ALREADY_EXISTS -3
#define S_ERR_NOT_EXISTS -4
#define S_ERR_FOUND_MULTI -5
#define S_ERR_BUFFER_LEN -6
#define S_ERR_INIT -7

#ifdef __cplusplus
}
#endif

#endif /* PTTERR_H */