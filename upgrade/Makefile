# $Id: Makefile 3673 2007-12-12 01:42:23Z kcwu $
# Usually you won't build everything for upgrading, so we let you decide what to run.

SRCROOT=	..
.include "$(SRCROOT)/pttbbs.mk"

CFLAGS+= -DPTTBBS_UTIL

BBSBASE=	$(SRCROOT)/include/var.h

UTIL_OBJS=	\
	util_cache.o	util_record.o	util_passwd.o	util_var.o	\
	util_stuff.o	util_osdep.o	util_args.o	util_file.o	\
	util_crypt.o	

LIBS+=  $(SRCROOT)/src/libbbsutil/libbbsutil.a \
	$(SRCROOT)/src/libbbs/libbbs.a

all:
	echo "Usually you won't build everything for upgrading, so we let you decide what to run."

$(SRCROOT)/include/var.h: $(SRCROOT)/mbbsd/var.c
	cd $(SRCROOT)/mbbsd; $(MAKE) $(SRCROOT)/include/var.h

r2014convert: r2014convert.c
	$(CCACHE) ${CC} ${CFLAGS} ${LDFLAGS} -o r2014convert r2014convert.c $(LIBS)
