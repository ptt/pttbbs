# $Id$
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

ENABLE_GEOIP_DB=y

PROGRAMS=	fromd fromc

LDLIBS+=$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a

FROMD_LDFLAGS = -levent

.if ${ENABLE_GEOIP_DB} == "y"
CFLAGS += -DFROMD_USE_GEOIP_DB
FROMD_LDFLAGS += -lGeoIP
.endif

all:	${PROGRAMS}

.SUFFIXES: .c .cpp .o
.c.o:
	$(CC) $(CFLAGS) -c $*.c
.cpp.o:
	$(CXX) $(CXXFLAGS) -c $*.cpp

fromd: fromd.o ip_desc_db.o country_names.o
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $> $(LDLIBS) $(FROMD_LDFLAGS)

fromc: fromc.o
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $> $(LDLIBS)

clean:
	rm -f *~ ${PROGRAMS} fromd.o fromc.o ip_desc_db.o country_names.o
