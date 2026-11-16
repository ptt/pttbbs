SRCROOT:=	..
include $(SRCROOT)/pttbbs.mk

# bof: Birds Of Feather, the meta layer between BBS and community systems.
# See README.md. The core (the API, its checks, communities, persons and
# rules), the files of a BBS (files/) and the files backend (fs/).

CORE_SRCS:=	bof.c modlog.c people.c policy.c readmarks.c storage.c
FILES_SRCS:=	files/comment.c files/content.c files/crosspost.c \
		files/record.c files/timecap.c
FS_SRCS:=	fs/backend.c fs/comment.c fs/content.c fs/crosspost.c \
		fs/listing.c fs/modlog.c fs/post.c fs/report.c
# The post.svc backend (bof_storage_postsvc); the default backend stays
# the files one unless pttbbs.conf says otherwise.
PS_SRCS:=	ps/postsvc.c
SRCS:=	$(CORE_SRCS) $(FILES_SRCS) $(FS_SRCS) $(PS_SRCS)
LIB:=	libbof.a
# For programs other than the BBS (gateways): BoF, a way to open it, and
# the globals of the BBS (SHM, ...), as util has them.
GATEWAY_LIB:=	libbof_gateway.a
OBJS:=	$(SRCS:.c=.o)

TESTS:=	tests/test_bof tests/test_post tests/test_modlog tests/test_readmarks \
		tests/test_storage tests/test_people tests/test_gateway \
		tests/test_postsvc
TEST_LDFLAGS:=	-L. -L$(SRCROOT)/common/bbs -L$(SRCROOT)/common/sys \
		-L$(SRCROOT)/common/osdep
TEST_LDLIBS:=	-lbof -lcmbbs -lcmsys -losdep $(LDLIBS)

.PHONY: all clean install test

all:	$(SRCROOT)/include/var.h $(LIB) $(GATEWAY_LIB)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(GATEWAY_LIB): $(OBJS) gateway.o gateway_var.o
	rm -f $@
	$(AR) rcs $@ $^

gateway.o: gateway.c bof_int.h $(SRCROOT)/include/bof.h $(SRCROOT)/include/bof_gateway.h
	$(CC) $(CFLAGS) -c $< -o $@

gateway_var.o: $(SRCROOT)/mbbsd/var.c $(SRCROOT)/include/var.h
	$(CC) $(CFLAGS) -D_BBS_UTIL_C_ -c -o $@ $<

# Links with the gateway library alone.
tests/test_gateway: tests/test_gateway.c $(GATEWAY_LIB)
	$(CC) $(CFLAGS) $< -o $@ -L. -L$(SRCROOT)/common/bbs -L$(SRCROOT)/common/sys \
		-L$(SRCROOT)/common/osdep -lbof_gateway -lcmbbs -lcmsys -losdep $(LDLIBS)

%.o: %.c bof_int.h $(SRCROOT)/include/bof.h $(SRCROOT)/include/bof_files.h $(SRCROOT)/include/bof_fs.h $(SRCROOT)/include/bof_storage.h
	$(CC) $(CFLAGS) -c $< -o $@

# The globals of the BBS (SHM, ...), as util has them.
tests/var.o: $(SRCROOT)/mbbsd/var.c $(SRCROOT)/include/var.h
	$(CC) $(CFLAGS) -D_BBS_UTIL_C_ -c -o $@ $<

tests/test_%: tests/test_%.c tests/bof_test.h tests/var.o $(LIB)
	$(CC) $(CFLAGS) -Itests $< tests/var.o -o $@ $(TEST_LDFLAGS) $(TEST_LDLIBS)

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

install:

clean:
	rm -f $(OBJS) $(LIB) $(GATEWAY_LIB) gateway.o gateway_var.o $(TESTS) tests/var.o
