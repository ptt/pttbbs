
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

OBJS=	file.o lock.o log.o net.o sort.o string.o time.o crypt.o osdep.o
TARGET=	libcmsys.a


.SUFFIXES: .c .o
.c.o:
	$(CCACHE) $(DIETCC) $(CC) $(CFLAGS) -c $*.c

all: $(TARGET)

install:

$(TARGET): $(OBJS)
	$(AR) cru $@ $(OBJS)
	ranlib $@

clean:
	rm -f $(OBJS) $(TARGET)
