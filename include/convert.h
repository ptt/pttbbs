/* $Id: convert.h 1374 2003-11-27 14:11:40Z victor $ */

#ifndef _BBS_CONVERT_H
#define _BBS_CONVERT_H

#ifdef CONVERT

typedef enum {
    CONV_NORMAL,
    CONV_UTF8,
} ConvertMode;

extern int (*convert_write)(VBUF *v, char c);
extern int (*convert_read)(VBUF *v, const void* buf, size_t len);
extern ConvertMode convert_mode;

extern void init_convert(void);
extern void set_converting_type(ConvertMode which);

// special macros for pfterm

#ifndef FT_DBCS_NOINTRESC
# define FT_DBCS_NOINTRESC ( \
        (cuser.uflag & UF_DBCS_NOINTRESC) || \
        (convert_mode == CONV_UTF8))
#endif // FT_DBCS_NOINTRESC

// TODO make DBCS_BIG5 containing all "unsafe" properties
#ifndef FT_DBCS_BIG5
#define FT_DBCS_BIG5(c1, c2) { \
    if (convert_mode == CONV_UTF8 && \
        b2u_ambiguous_width[((unsigned int)c1 << 8) | c2]) \
        return FTDBCS_UNSAFE; \
    }
#endif 

#endif // CONVERT
#endif // _BBS_CONVERT_H
