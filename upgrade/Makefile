# Usually you won't build everything for upgrading, so we let you decide what to run.

SRCROOT=	..
include $(SRCROOT)/pttbbs.mk

CFLAGS+= -DPTTBBS_UTIL

BBSBASE=	$(SRCROOT)/include/var.h

UTIL_OBJS=	../util/util_var.o

LDLIBS:=  $(UTIL_OBJS) \
	  $(SRCROOT)/common/bbs/libcmbbs.a \
	  $(SRCROOT)/common/sys/libcmsys.a \
	  $(SRCROOT)/common/osdep/libosdep.a \
	  $(LDLIBS)

.PHONY: all

all:
	@echo "Usually you won't build everything for upgrading, "
	@echo "        so we let you decide what to run."
	@echo "Please type make rDDDD_XXXX to build your upgrade program."

convert_sgr_66: convert_sgr_66.c sgr66.o common.o
convert_utf8: convert_utf8.c sgr66.o common.o
fix_2026h: fix_2026h.c common.o
dir_bottom_to_bh: dir_bottom_to_bh.c

#$(SRCROOT)/include/var.h: $(SRCROOT)/mbbsd/var.c
#	cd $(SRCROOT)/mbbsd; $(MAKE) $(SRCROOT)/include/var.h
