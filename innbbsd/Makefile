# ------------------------------------------------------- #
#  innbbsd/Makefile	( NTHU CS MapleBBS Ver 2.36 )     #
# ------------------------------------------------------- #
#  target : Makefile for 轉信程式 innbbsd by skhuang      #
#  create : 95/03/29                                      #
#  update : 95/12/15                                      #
# ------------------------------------------------------- #
# stdarg.h patch for pttbbs by in2 03/05/05               #

OSTYPE!=uname
####################################################
# this is a bbs <--> news gateway
#####################################################
VERSION=0.51
LASTVERSION=0.50beta-4
ADMINUSER?= root@your.domain.name
BBSHOME?=$(HOME)
BBS_SRC = ..
TARGET  = $(INNBBSD) $(BBSNNRP) $(BBSLINK)
####################################################
CC=gcc
INSTALL=cp
#
OPT= -O -s -pipe -fomit-frame-pointer

BBS_DEP = MapleBBS 
BBS_UTIL = $(BBS_SRC)/util
BBS_REC = $(BBS_UTIL)/util_record.o $(BBS_UTIL)/util_cache.o \
	  $(BBS_UTIL)/util_passwd.o $(BBS_UTIL)/util_stuff.o \
	  $(BBS_UTIL)/util_var.o $(BBS_REC_EXTRA_$(OSTYPE))

BBS_REC_EXTRA_Linux = $(BBS_UTIL)/util_osdep.o

#########################
#
###############
DEBUGOBJ = /usr/lib/debug/mallocmap.o
CFLAGS+= -c -I. -I$(BBS_SRC)/include -I$(BBS_SRC)/mbbsd -D$(BBS_DEP) \
	 -I/usr/local/include -L/usr/local/lib \
	 $(EXTRAFLAGS) -DDBZDEBUG -DBBSHOME='"$(BBSHOME)"' \
	 -D_PATH_BBSHOME=\"$(BBSHOME)\" \
	 -DVERSION=\"$(VERSION)\" -DADMINUSER=\"$(ADMINUSER)\" 
LDFLAGS+= -liconv
#
####################################################

OBJS = inndchannel.o innbbsd.o connectsock.o rfc931.o \
       daemon.o file.o pmain.o his.o dbz.o \
       closeonexec.o dbztool.o inntobbs.o receive_article.o \
       echobbslib.o str_decode.o $(BBS_REC)
       # $(BBS_REC)
SRCS = inndchannel.c innbbsd.c connectsock.c rfc931.c \
       daemon.c file.c pmain.c parsdate.y his.c dbz.c \
       closeonexec.c dbztool.c inntobbs.c bbslib.c receive_article.c \
       port.c str_decode.c

MOBJS    = makedbz.o bbslib.o file.o dbz.o closeonexec.o 
HOBJS    = mkhistory.o bbslib.o file.o his.o dbz.o port.o closeonexec.o
DBZOBJS  = dbzserver.o dbzinnbbsd.o pmain.o connectsock.o bbslib.o his.o \
	   daemon.o file.o dbz.o dbztool.o rfc931.o port.o closeonexec.o \
           morelog.o
NNRPOBJS = bbsnnrp.o pmain.o bbslib.o connectsock.o file.o 
LINKOBJS = bbslink.o pmain.o inntobbs.o echobbslib.o connectsock.o file.o port.o\
           str_decode.o
LINKOBJS2 = bbslink2.o pmain.o inntobbs.o echobbslib.o connectsock.o file.o port.o
CTLOBJS  = ctlinnbbsd.o pmain.o bbslib.o connectsock.o file.o 
INNBBSD  = innbbsd mkhistory bbsnnrp ctlinnbbsd $(BBSLINK)
BBSNNRP  = bbsnnrp
BBSLINK  = bbslink
BBSLINK2  = bbslink2
EXES     = $(TARGET)

.c.o:
	$(CC) $(OPT) $(CFLAGS) $*.c


all: $(OSTYPE)

sun: 
	@$(MAKE) EXTRAFLAGS="-DMMAP -DGETRUSAGE -DMALLOCMAP" LDFLAGS="$(DEBUGOBJ)" target


aix: 
	@$(MAKE) CC=cc EXTRAFLAGS="-DMMAP -DGETRUSAGE -DAIX -DSYSV" target

Linux: 
	@$(MAKE) EXTRAFLAGS="-DLINUX -DGETRUSAGE" target

hpux: 
	@$(MAKE) OPT= EXTRAFLAGS="-DMMAP -DHPUX -DSYSV" target

irix: 
	@$(MAKE) EXTRAFLAGS="-DMMAP -DIRIX -DSYSV" target

solaris:
	@$(MAKE) EXTRAFLAGS="-DMMAP -DSOLARIS -DSYSV" LDFLAGS="-lsocket -lnsl" YACC="bison -y" target

FreeBSD:
	@$(MAKE) CC=cc EXTRAFLAGS="-DBSD44 -DMMAP -DGETRUSAGE" LIBCRYPT=-lcrypt  target

bsd:
	@$(MAKE) CC=cc EXTRAFLAGS="-DMMAP -DGETRUSAGE" target

alpha:
	@$(MAKE) CC=cc EXTRAFLAGS="-DMMAP -DDEC_ALPHA -DGETRUSAGE" target

sysv: 
	@$(MAKE) OPT= EXTRAFLAGS="-DMMAP -DSYSV" target

target: $(EXES)

r_port.o: r_port.c
	$(CC) $(OPT) $(CFLAGS) -DWITH_RECORD_O r_port.c 

echobbslib.o: echobbslib.c
	$(CC) $(OPT) $(CFLAGS) -DWITH_ECHOMAIL echobbslib.c

tar: 
	test -d innbbsd-$(VERSION) || mkdir innbbsd-$(VERSION)
	cp *.c *.h Makefile innbbsd-$(VERSION)
	tar zcf innbbsd-$(VERSION).tar.gz innbbsd-$(VERSION)
	cp innbbsd-$(VERSION).tar.gz /net/ftphome/CSIE/innbbsd

patch:
	rm -rf /tmp/innbbsd-$(LASTVERSION)
	zcat /net/ftphome/ftp-data/bbs/misc/innbbsd/innbbsd-$(LASTVERSION).tar.gz | (cd /tmp; tar xf -)	
	(cd /tmp/innbbsd-$(VERSION); make clean; rm -f r_port.c dbzserver.c dbzinnbbsd.c  echobbslib.c )
	(cd /tmp/innbbsd-$(LASTVERSION); make clean; rm -f r_port.c dbzserver.c dbzinnbbsd.c  echobbslib.c )
	(cd /tmp; diff -rcs innbbsd-$(LASTVERSION) innbbsd-$(VERSION) > /tmp/innbbsd-patch.$(LASTVERSION)-$(VERSION); echo "diff -rcs")
	(cd /tmp/innbbsd-$(VERSION); ln -s port.c r_port.c)
	(cd /tmp/innbbsd-$(VERSION); ln -s inndchannel.c dbzserver.c)
	(cd /tmp/innbbsd-$(VERSION); ln -s innbbsd.c dbzinnbbsd.c)
	(cd /tmp/innbbsd-$(VERSION); ln -s bbslib.c echobbslib.c)


distribution:
	rm -rf  /tmp/innbbsd-$(VERSION)
	test -d /tmp/innbbsd-$(VERSION) || mkdir /tmp/innbbsd-$(VERSION)
	test -d /tmp/innbbsd-$(VERSION)/innd || mkdir /tmp/innbbsd-$(VERSION)/innd
	rm -f r_port.c
	rm -f dbzserver.c
	rm -f dbzinnbbsd.c
	rm -f echobbslib.c
	cp -r doc hisconvert FEATURES CHANGES README* TODO *.c *.h *.y Makefile boards /tmp/innbbsd-$(VERSION)
	(cd /tmp/innbbsd-$(VERSION); ln -s port.c r_port.c)
	(cd /tmp/innbbsd-$(VERSION); ln -s inndchannel.c dbzserver.c)
	(cd /tmp/innbbsd-$(VERSION); ln -s innbbsd.c dbzinnbbsd.c)
	(cd /tmp/innbbsd-$(VERSION); ln -s bbslib.c echobbslib.c)
	(cd innd ; cp -r Makefile README innbbs.conf in.bntpd mailpost *.pl bntplink bntplink.palmbbs *.active* *.c *.y *.bbs src /tmp/innbbsd-$(VERSION)/innd)
	(cd /tmp/innbbsd-$(VERSION); make clean)
	(cd /tmp; tar zcf innbbsd-$(VERSION).tar.gz innbbsd-$(VERSION) ) 
	ln -s port.c r_port.c
	ln -s inndchannel.c dbzserver.c
	ln -s innbbsd.c dbzinnbbsd.c
	ln -s bbslib.c echobbslib.c

remotetest: distribution
	/usr/local/krb4/rcp /tmp/innbbsd-$(VERSION).tar.gz bbs_src.tar.gz skhuang:/tmp
	/usr/local/krb4/rcp /tmp/innbbsd-$(VERSION).tar.gz bbs_src.tar.gz linux:/tmp
	mv /u/staff/skhuang/.tcshrc /u/staff/skhuang/skhuang.tcshrc
	rcp /tmp/innbbsd-$(VERSION).tar.gz bbs_src.tar.gz ccsun36:/tmp
	rcp /tmp/innbbsd-$(VERSION).tar.gz bbs_src.tar.gz ccibm1:/tmp
	mv /u/staff/skhuang/skhuang.tcshrc /u/staff/skhuang/.tcshrc
	rcp /tmp/innbbsd-$(VERSION).tar.gz bbs_src.tar.gz cciris3:/tmp

ftp: distribution patch
	rsh ccsun42 mv /home8/ftp/pub/bbs/misc/innbbsd/innbbsd-$(VERSION).tar.gz /home8/ftp/pub/bbs/misc/innbbsd/.innbbsd-$(VERSION).tar.gz
	rcp /tmp/innbbsd-$(VERSION).tar.gz ccsun42:/home8/ftp/pub/bbs/misc/innbbsd
	rcp README* ccsun42:/home8/ftp/pub/bbs/misc/innbbsd
	rcp /tmp/innbbsd-patch.$(LASTVERSION)-$(VERSION) ccsun42:/home8/ftp/pub/bbs/misc/innbbsd

innbbspost.o: innbbspost.c
	$(CC) $(CFLAGS) -I$(BBS_SRC) -c innbbspost.c 

makedbz: $(MOBJS)
	$(CC) $(OPT) -o makedbz $(MOBJS) $(LDFLAGS)

dbzserver.o: dbzserver.c
	$(CC) $(CFLAGS) -DDBZSERVER -I$(BBS_SRC) -c dbzserver.c   

dbzinnbbsd.o: dbzinnbbsd.c
	$(CC) $(CFLAGS) -DDBZSERVER -I$(BBS_SRC) -c dbzinnbbsd.c   

dbzserver: $(DBZOBJS)
	$(CC) $(OPT) -o dbzserver $(DBZOBJS) $(LDFLAGS)

bbsnnrp: $(NNRPOBJS)
	$(CC) -o bbsnnrp $(NNRPOBJS) $(OPT) $(LDFLAGS)

bbslink: $(LINKOBJS)
	$(CC) -o bbslink $(LINKOBJS) $(OPT) $(LDFLAGS)

bbslink2: $(LINKOBJS2)
	$(CC) -o bbslink2 $(LINKOBJS2) $(OPT) $(LDFLAGS)

ctlinnbbsd: $(CTLOBJS)
	$(CC) $(OPT) -o ctlinnbbsd $(CTLOBJS) $(LDFLAGS)

mkhistory: $(HOBJS)
	$(CC) $(OPT) -o mkhistory $(HOBJS) $(LDFLAGS)

dbz_query_sample: dbz_query_sample.o
	$(CC) $(OPT) -o dbz_query_sample dbz_query_sample.o $(LDFLAGS)


innbbsd: $(OBJS)
#	mv tnrpd.exe tnrpd.exe.old
	$(CC) -o innbbsd $(OBJS) $(OPT) $(LDFLAGS)

install: $(EXES)
	install -d $(BBSHOME)/innd/
	install -c -m 755 $(EXES) $(BBSHOME)/innd/

clean:
	rm -f *.o $(EXES) core innd/src/*.o
