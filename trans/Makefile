# $Id$

SRCROOT=	..
.include "$(SRCROOT)/pttbbs.mk"

CFLAGS+= -DPTTBBS_UTIL

BBSBASE=	$(SRCROOT)/include/var.h

UTIL_OBJS=	\
	util_cache.o	util_record.o	util_passwd.o	util_var.o

MBBSD_OBJS= \
	cache		record		passwd		var

# 下面這些程式, 會被 compile 並且和 $(UTIL_OBJS) 聯結
CPROG_WITH_UTIL= \
	itoc_trans \
	sob_trans \
	transman

# 下面是 C++ 的程式
CPP_WITH_UTIL= \


# 下面這些程式, 會直接被 compile
CPROG_WITHOUT_UTIL= \

# 下面這些程式會被 install
PROGS=	${CPROG_WITH_UTIL}	${CPROG_WITHOUT_UTIL}	${CPP_WITH_UTIL}\

LDLIBS+=$(SRCROOT)/common/osdep/libosdep.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/bbs/libcmbbs.a


all: $(SRCROOT)/include/var.h ${CPROG_WITH_UTIL} ${CPROG_WITHOUT_UTIL} ${CPP_WITH_UTIL} ${PROGS}

.for fn in ${CPROG_WITH_UTIL}
${fn}: ${BBSBASE} ${fn}.c ${UTIL_OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o ${fn} ${UTIL_OBJS} ${fn}.c $(LDLIBS)
.endfor

.for fn in ${CPP_WITH_UTIL}
${fn}: ${BBSBASE} ${fn}.cc ${UTIL_OBJS}
	${CXX} ${CXXFLAGS} ${LDFLAGS} -o ${fn} ${UTIL_OBJS} ${fn}.cc $(LDLIBS)
.endfor

.for fn in ${MBBSD_OBJS}
util_${fn}.o: ${BBSBASE} $(SRCROOT)/mbbsd/${fn}.c
	${CC} ${CFLAGS} -D_BBS_UTIL_C_ -c -o $@ $(SRCROOT)/mbbsd/${fn}.c
.endfor

install: $(PROGS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGS) $(BBSHOME)/bin/

clean:
	rm -f *.o $(CPROGS) $(CPROG_WITH_UTIL) $(CPROG_WITHOUT_UTIL) $(CPP_WITH_UTIL)

