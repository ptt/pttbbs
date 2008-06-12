
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

OBJS=	log.o file.o money.o names.o path.o time.o string.o
# record.o
TARGET=	libcmbbs.a


.SUFFIXES: .c .o
.c.o:	$(SRCROOT)/include/var.h
	$(CCACHE) $(DIETCC) $(CC) $(CFLAGS) -c $*.c

all: $(TARGET)

install:

$(SRCROOT)/include/var.h:	$(SRCROOT)/mbbsd/var.c
	perl $(SRCROOT)/util/parsevar.pl < $(SRCROOT)/mbbsd/var.c > $(SRCROOT)/include/var.h

$(TARGET): $(OBJS)
	$(AR) cru $@ $(OBJS)
	ranlib $@

clean:
	rm -f $(OBJS) $(TARGET)
