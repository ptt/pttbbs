# $Id: Makefile 3673 2007-12-12 01:42:23Z kcwu $
# Usually you won't build everything for upgrading, so we let you decide what to run.

SRCROOT=	..
.include "$(SRCROOT)/pttbbs.mk"

CFLAGS+= -DPTTBBS_UTIL

BBSBASE=	$(SRCROOT)/include/var.h

UTIL_OBJS=	\
	util_cache.o	util_record.o	util_passwd.o	util_var.o	\
	util_stuff.o	

LDLIBS+=  $(SRCROOT)/common/sys/libcmsys.a \
	  $(SRCROOT)/common/bbs/libcmbbs.a

all:
	echo "Usually you won't build everything for upgrading, so we let you decide what to run."
	echo "Please type make rDDDD_XXXX to build your upgrade program."

$(SRCROOT)/include/var.h: $(SRCROOT)/mbbsd/var.c
	cd $(SRCROOT)/mbbsd; $(MAKE) $(SRCROOT)/include/var.h
