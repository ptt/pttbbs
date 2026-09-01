SRCROOT:=	../..
include $(SRCROOT)/pttbbs.mk

SRCS:=	verifydb.fbs register_sms.fbs
HDRS:=	$(SRCS:=.h)

.PHONY: all clean install

all: $(HDRS)

%.fbs.h: %.fbs
	flatc --cpp $< --filename-suffix .fbs

install:

clean:
	@rm -f $(HDRS)
