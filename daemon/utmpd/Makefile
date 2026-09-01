SRCROOT ?= ../..
include $(SRCROOT)/pttbbs.mk

PROGRAMS := utmpsync utmpserver3
UTILDIR  := $(SRCROOT)/util
UTILOBJ  := $(UTILDIR)/util_var.o

LDLIBS += $(SRCROOT)/common/bbs/libcmbbs.a \
	  $(SRCROOT)/common/sys/libcmsys.a \
	  $(SRCROOT)/common/osdep/libosdep.a

.PHONY: all clean

all: $(PROGRAMS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

utmpserver3: utmpserver3.o friend.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) -levent

utmpsync: utmpsync.o $(UTILOBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f *~ $(PROGRAMS) friend.o utmpserver3.o utmpsync.o
