#ifndef _PIERR_H_
#define _PIERR_H_

#define	PIERR_OK	(int32)0	/* no error */
#define	PIERR_INT	(int32)1	/* internal error (ex: syscall failure) */
#define	PIERR_NOBRD	(int32)2	/* no such board or permission denied */
#define	PIERR_NOMORE	(int32)3	/* no more data */
#define	PIERR_NOTCLASS	(int32)4	/* this bid is NOT class */
#endif
