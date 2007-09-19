# $Id$
SRCROOT=	..
.include "$(SRCROOT)/pttbbs.mk"

VERSION=	0.50-pttpatch
ADMINUSER?=	root@your.domain.name

# FreeBSD innbbsd肂惠把计
inn_CFLAGS_FreeBSD=	-DBSD44 -DMMAP -DGETRUSAGE
inn_LDFLAGS_FreeBSD=	-L/usr/local/lib -lcrypt -liconv

# Linux innbbsd肂惠把计
inn_CFLAGS_Linux=	-DLINUX -DGETRUSAGE
inn_LDFLAGS_Linux=	

# Solarisinnbbsd肂惠把计
inn_CFLAGS_Solaris=	-DMMAP -DSolaris -DSYSV -I/usr/local/include/
inn_LDFLAGS_Solaris=	-L/usr/local/lib -liconv -lsocket -lnsl -lkstat

CFLAGS+=	-DVERSION=\"${VERSION}\"		\
		-DADMINUSER=\"${ADMINUSER}\"		\
		-DMapleBBS	-DDBZDEBUG	-I.	\
		${inn_CFLAGS_${OSTYPE}} -DHMM_USE_ANTI_SPAM

LDFLAGS+=	${inn_LDFLAGS_${OSTYPE}}

PROGS=		bbslink		bbsnnrp		ctlinnbbsd	\
		innbbsd		mkhistory

all:	${PROGS}

# bbs util
UTIL_DIR=	$(SRCROOT)/util
UTIL_OBJS=	\
	util_cache.o	util_record.o	util_passwd.o	util_var.o	\
	util_stuff.o	util_osdep.o	util_args.o	util_file.o

.for fn in ${UTIL_OBJS}
LINK_UTIL_OBJS+=	${UTIL_DIR}/${fn}

${UTIL_DIR}/${fn}:	# FIXME: dependency
	cd ${UTIL_DIR}; make ${fn}
.endfor


echobbslib.o:	echobbslib.c
	${CC} ${CFLAGS} -DWITH_ECHOMAIL -c echobbslib.c

innbbsd:	inndchannel.o innbbsd.o connectsock.o rfc931.o daemon.o \
		file.o pmain.o his.o dbz.o  closeonexec.o dbztool.o \
		inntobbs.o receive_article.o  echobbslib.o str_decode.o nocem.o
	${CCACHE} ${CC} -o $@ ${LDFLAGS} $? ${LINK_UTIL_OBJS}

bbslink:	bbslink.o pmain.o inntobbs.o echobbslib.o connectsock.o \
		file.o port.o str_decode.o
	${CCACHE} ${CC} -o $@ ${LDFLAGS} $? ${LINK_UTIL_OBJS}

bbsnnrp:	bbsnnrp.o pmain.o bbslib.o connectsock.o file.o
	${CCACHE} ${CC} -o $@ ${LDFLAGS} $? ${LINK_UTIL_OBJS}

ctlinnbbsd:	ctlinnbbsd.o pmain.o bbslib.o connectsock.o file.o
	${CCACHE} ${CC} -o $@ ${LDFLAGS} $? ${LINK_UTIL_OBJS}

mkhistory:	mkhistory.o bbslib.o file.o his.o dbz.o port.o closeonexec.o
	${CCACHE} ${CC} -o $@ ${LDFLAGS} $? ${LINK_UTIL_OBJS}

install:	${PROGS}
	install -d ${BBSHOME}/innd/
	install -c -m 755 ${PROGS} ${BBSHOME}/innd/

clean:
	rm -f *.o ${PROGS} core *.core
