SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

.SUFFIXES: .proto .pb.cc .pb.h .grpc.pb.cc .grpc.pb.h

all: mand manc

GENERATED_SRCS=man.pb.cc man.pb.h man.grpc.pb.cc man.grpc.pb.h

$(GENERATED_SRCS): man.proto
	protoc -I. --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin man.proto

MANC_OBJS=manc.o man.grpc.pb.o man.pb.o

manc: $(MANC_OBJS)
	$(CXX) -o $@ $(MANC_OBJS) $(LDFLAGS) $(LDADD)

MAND_OBJS=\
  article.o \
  convert.o \
  evbuffer.o \
  file_header.o \
  man.grpc.pb.o \
  man.pb.o \
  man_directory_impl.o \
  man_path.o \
  man_service_impl.o \
  mand.o \
  util.o

mand: $(MAND_OBJS)
	$(CXX) -o $@ $(MAND_OBJS) $(LDFLAGS) $(LDADD)

UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

COMMONFLAGS= -I$(SRCROOT) -I../boardd
CXXFLAGS+=	-std=c++11 $(COMMONFLAGS)
CFLAGS+=	$(LIBEVENT_CFLAGS) $(COMMONFLAGS)
LDFLAGS+=

LDADD:=  $(UTILOBJ) \
	$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	$(LIBEVENT_LIBS_l) -levent_pthreads \
	-pthread \
	-lgrpc++ -lprotobuf -lgflags \
	$(LDADD)

clean:
	rm -f $(MAND_OBJS) $(MANC_OBJS) $(GENERATED_SRCS) mand manc

.include <bsd.prog.mk>
