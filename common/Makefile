SRCROOT=        ..
include $(SRCROOT)/pttbbs.mk

SUBDIRS:=	bbs osdep sys

ifneq ($(strip $(USE_MBBSD_CXX)),)
SUBDIRS+=	fbs
endif

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

install: all
	@true

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || exit 1; \
	done
