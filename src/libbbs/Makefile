
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

CFLAGS+=	-I$(SRCROOT)/include

OBJS=	log.o	string.o money.o
TARGET=	libbbs.a


.SUFFIXES: .c .o
.c.o:
	$(CCACHE) $(DIETCC) $(CC) $(CFLAGS) -c $*.c

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) cru $@ $(OBJS)
	ranlib $@

clean:
	rm -f $(OBJS) $(TARGET)
