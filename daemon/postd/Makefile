SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGRAMS=	rebuild
UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

LDLIBS:=$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	-levent $(LDLIBS)

all:	${PROGRAMS}

.SUFFIXES: .c .cpp .o
.c.o:
	$(CC) $(CFLAGS) -c $*.c
.cpp.o:
	$(CXX) $(CXXFLAGS) -c $*.cpp

rebuild: $*.o
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $> $(UTILOBJ) $(LDLIBS)

install: $(PROGRAMS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGRAMS) $(BBSHOME)/bin/

clean:
	rm -f *~ ${PROGRAMS} *.o
