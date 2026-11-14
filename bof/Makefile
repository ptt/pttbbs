SRCROOT:=	..
include $(SRCROOT)/pttbbs.mk

# bof: Birds Of Feather, the meta layer between BBS and community systems.
# See README.md. The core (the API and its checks).

CORE_SRCS:=	bof.c
SRCS:=	$(CORE_SRCS)
LIB:=	libbof.a
OBJS:=	$(SRCS:.c=.o)

TESTS:=	tests/test_bof
TEST_LDFLAGS:=	-L. -L$(SRCROOT)/common/bbs -L$(SRCROOT)/common/sys \
		-L$(SRCROOT)/common/osdep
TEST_LDLIBS:=	-lbof -lcmbbs -lcmsys -losdep $(LDLIBS)

.PHONY: all clean install test

all:	$(SRCROOT)/include/var.h $(LIB)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

%.o: %.c bof_int.h $(SRCROOT)/include/bof.h
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_%: tests/test_%.c tests/bof_test.h $(LIB)
	$(CC) $(CFLAGS) -Itests $< -o $@ $(TEST_LDFLAGS) $(TEST_LDLIBS)

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

install:

clean:
	rm -f $(OBJS) $(LIB) $(TESTS)
