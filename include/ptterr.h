/* $Id$ */
#ifndef PTTERR_H
#define PTTERR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef char Err;

#define S_OK 0
#define S_ERR -1                    // 255
#define S_ERR_MALLOC -2             // 254
#define S_ERR_ALREADY_EXISTS -3     // 253
#define S_ERR_NOT_EXISTS -4         // 252
#define S_ERR_FOUND_MULTI -5        // 251
#define S_ERR_BUFFER_LEN -6         // 250
#define S_ERR_INIT -7               // 249
#define S_ERR_ABORT_BBS -8          // 248
#define S_ERR_BUSY -9               // 247
#define S_ERR_SLEEP -10             // 246
#define S_ERR_NO_KEY -11            // 245
#define S_ERR_EDIT_LOCK -12         // 244
#define S_ERR_EXTEND -13            // 243
#define S_ERR_NOT_INIT -14          // 242
#define S_ERR_SAVE -15              // 241

#ifdef __cplusplus
}
#endif

#endif /* PTTERR_H */