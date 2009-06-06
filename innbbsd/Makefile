# $Id$
SRCROOT=	..
.include "${SRCROOT}/pttbbs.mk"

VERSION=	0.50-pttpatch
ADMINUSER?=	root@your.domain.name

.if $(OSTYPE) == "FreeBSD"
# FreeBSD innbbsd肂惠把计
inn_CFLAGS=	-DMMAP -DGETRUSAGE
inn_LDFLAGS=	-L/usr/local/lib -liconv
.else
# Linux innbbsd肂惠把计
inn_CFLAGS=	-DGETRUSAGE
inn_LDFLAGS=	
.endif

CFLAGS+=	-DVERSION=\"${VERSION}\"		\
		-DADMINUSER=\"${ADMINUSER}\"		\
		-DMapleBBS	-DDBZDEBUG	-I.	\
		${inn_CFLAGS} -DHMM_USE_ANTI_SPAM

LDFLAGS+=	${inn_LDFLAGS}

PROGS=		bbslink		bbsnnrp		ctlinnbbsd	\
		innbbsd		mkhistory

all:	${PROGS}

# bbs util
UTIL_DIR=	$(SRCROOT)/util
UTIL_OBJS=	\
	util_cache.o	util_passwd.o	util_var.o

.for fn in ${UTIL_OBJS}
LINK_UTIL_OBJS+=	${UTIL_DIR}/${fn}

${UTIL_DIR}/${fn}:	# FIXME: dependency
	cd ${UTIL_DIR}; make ${fn}
.endfor

LDLIBS+=$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a	

echobbslib.o:	echobbslib.c
	${CC} ${CFLAGS} -DWITH_ECHOMAIL -c echobbslib.c

innbbsd:	inndchannel.o innbbsd.o connectsock.o rfc931.o daemon.o \
		file.o pmain.o his.o dbz.o  closeonexec.o dbztool.o \
		inntobbs.o receive_article.o  echobbslib.o nocem.o
	${CC} -o $@ ${LDFLAGS} $> ${LINK_UTIL_OBJS} $(LDLIBS)

bbslink:	bbslink.o pmain.o inntobbs.o echobbslib.o connectsock.o \
		file.o port.o 
	${CC} -o $@ ${LDFLAGS} $> ${LINK_UTIL_OBJS} $(LDLIBS)

bbsnnrp:	bbsnnrp.o pmain.o bbslib.o connectsock.o file.o
	${CC} -o $@ ${LDFLAGS} $> ${LINK_UTIL_OBJS} $(LDLIBS)

ctlinnbbsd:	ctlinnbbsd.o pmain.o bbslib.o connectsock.o file.o
	${CC} -o $@ ${LDFLAGS} $> ${LINK_UTIL_OBJS} $(LDLIBS)

mkhistory:	mkhistory.o bbslib.o file.o his.o dbz.o port.o closeonexec.o
	${CC} -o $@ ${LDFLAGS} $> ${LINK_UTIL_OBJS} $(LDLIBS)

install:	${PROGS}
	install -d ${BBSHOME}/innd/
	install -c -m 755 ${PROGS} ${BBSHOME}/innd/

clean:
	rm -f *.o ${PROGS} core *.core

reset:
	cd $(SRCROOT)/util/; ${MAKE} clean shmctl
	${MAKE} clean ; ${MAKE} ; ${MAKE} install
	killall innbbsd
	cd ~; nohup innd/innbbsd 7777 &
