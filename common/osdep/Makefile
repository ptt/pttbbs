SRCROOT=	../..
include $(SRCROOT)/pttbbs.mk

SRCS:=	cpuload.c proctitle.c strlcat.c strlcpy.c memusage.c
LIB:=	libosdep.a
OBJS:=	$(SRCS:.c=.o)

.PHONY: all clean install

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install:

clean:
	rm -f $(OBJS) $(LIB)
