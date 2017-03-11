# $Id$

SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

.SUFFIXES: .proto .pb.cc .pb.h .grpc.pb.cc .grpc.pb.h

PROG=	boardd
SRCS=	boardd.c convert.c article.c server.cpp board.pb.cc board.grpc.pb.cc strings.cpp board_service_impl.cpp bbs++.cpp evbuffer.cpp
MAN=

UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

COMMONFLAGS= -I$(SRCROOT)
CXXFLAGS+=	-std=c++11 $(COMMONFLAGS)
CFLAGS+=	$(LIBEVENT_CFLAGS) $(COMMONFLAGS)
LDFLAGS+=	$(LIBEVENT_LIBS_L)

LDADD:=  $(UTILOBJ) \
	$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	$(LIBEVENT_LIBS_l) -levent_pthreads \
	-pthread -lstdc++ -lboost_system \
	-l:libgrpc++.a -l:libgrpc.a -l:libprotobuf.a -l:libgflags.a \
	$(LDADD)

GENERATED_SRCS=board.pb.cc board.pb.h board.grpc.pb.cc board.grpc.pb.h

$(GENERATED_SRCS): board.proto
	protoc -I. --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin board.proto

BOARDC_OBJS=boardc.o board.grpc.pb.o board.pb.o

boardc: $(GENERATED_SRCS) $(BOARDC_OBJS)
	$(CXX) -o $@ $(BOARDC_OBJS) $(LDFLAGS) $(LDADD)

clean:
	rm -f $(BOARDC_OBJS) $(GENERATED_SRCS) boardc boardd

all: boardc boardd

.include <bsd.prog.mk>
