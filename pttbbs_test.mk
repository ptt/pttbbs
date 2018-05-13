#######################################################################
# dir
#######################################################################

MBBSD_DIR:=	${SRCROOT}/mbbsd
TEST_MBBSD_DIR:= ${TESTROOT}/test_mbbsd

#######################################################################
# lib
#######################################################################
CFLAGS+=	-I$(TESTROOT)/include
CXXFLAGS+=	-I$(TESTROOT)/include

LDFLAGS+=	-L$(TESTROOT)/util
LDLIBS+=	-ltest_util

#######################################################################
#
# mbbsd/Makefile
#
#######################################################################

.include "$(SRCROOT)/pttbbs_mbbsd.mk"

#######################################################################
# MBBSDOBJ
#######################################################################

MBBSD_OBJS	= ${TEST_MBBSD_DIR}/mbbsd_mock.o ${TEST_MBBSD_DIR}/vers_mock.o
.for obj in ${OBJS}
MBBSD_OBJS+= ${MBBSD_DIR}/${obj}
.endfor	

##########
# gtest
##########

CPPFLAGS+=	-isystem $(GTEST_DIR)/include
CXXFLAGS+=	-g -Wall -Wextra -pthread

GTEST_HEADERS=	/usr/include/gtest/*.h \
		/usr/include/gtest/internal/*.h

GTEST_SRCS_= $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_HEADERS)

AR:=		ar
ARFLAGS:=	rv

##########
# TARGETS
##########

.if defined(GTEST_DIR)
##########
# gtest
##########
gtest-all.o: $(GTEST_SRCS_)
	$(CXX) -I$(GTEST_DIR) $(CXXFLAGS) -c $(GTEST_DIR)/src/gtest-all.cc

gtest_main.o: $(GTEST_SRCS_)
	$(CXX) -I$(GTEST_DIR) $(CXXFLAGS) -c $(GTEST_DIR)/src/gtest_main.cc

gtest.a: gtest-all.o
	$(AR) $(ARFLAGS) $@ $>

gtest_main.a: gtest-all.o gtest_main.o
	$(AR) $(ARFLAGS) $@ $>

##########
# all
##########

.SUFFIXES: .c .o

${TEST_MBBSD_DIR}/vers_mock.o: ${MBBSD_DIR}/vers.c
	$(CC) $(CFLAGS) -c $> -o $@

.c.o:	$(SRCROOT)/include/var.h
	$(CC) $(CFLAGS) -c $*.c

.SUFFIXES: .cc .o

.cc.o: $(SRCROOT)/include/var.h
	$(CXX) $(CXXFLAGS) -c $*.cc

.for fn in ${TEST_PROGS}
${fn}: ${fn}.o gtest.a ${MBBSD_OBJS}
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $> $(LDLIBS)
.endfor


all: $(TEST_PROGS)

.else # GTEST_DIR

all:

.endif # GTEST_DIR


install:
	
clean:
	rm -f *.o gtest.a gtest_main.a $(TEST_PROGS)
