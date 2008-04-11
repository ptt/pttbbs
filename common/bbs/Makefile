
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

CFLAGS+=	-I$(SRCROOT)/include

OBJS=	log.o file.o money.o names.o path.o time.o string.o
# record.o
TARGET=	libcmbbs.a


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
