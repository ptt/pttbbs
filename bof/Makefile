SRCROOT:=	..
include $(SRCROOT)/pttbbs.mk

# bof: Birds Of Feather, the meta layer between BBS and community systems.
# See README.md. The core (the API and its checks), the files of a BBS
# (files/) and the files backend (fs/).

CORE_SRCS:=	bof.c storage.c
FILES_SRCS:=	files/comment.c files/content.c \
		files/record.c files/timecap.c
FS_SRCS:=	fs/backend.c fs/comment.c \
		fs/post.c
# Other backends live where their services are, e.g. post.svc's in
# bof/ps/ of pttbbs.cmd; whatever is there is built in.
PS_SRCS:=	$(sort $(wildcard ps/*.c))
SRCS:=	$(CORE_SRCS) $(FILES_SRCS) $(FS_SRCS) $(PS_SRCS)
LIB:=	libbof.a
OBJS:=	$(SRCS:.c=.o)

TESTS:=	tests/test_bof tests/test_post \
		tests/test_storage
TEST_LDFLAGS:=	-L. -L$(SRCROOT)/common/bbs -L$(SRCROOT)/common/sys \
		-L$(SRCROOT)/common/osdep
TEST_LDLIBS:=	-lbof -lcmbbs -lcmsys -losdep $(LDLIBS)

.PHONY: all clean install test

all:	$(SRCROOT)/include/var.h $(LIB)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

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
	rm -f $(OBJS) $(LIB) $(TESTS) tests/var.o
