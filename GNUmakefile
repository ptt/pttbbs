# For the config, read once here for every sub-make.
SRCROOT:=	.
include pttbbs.mk

SUBDIRS:=	common bof mbbsd util service

# Local services
SUBDIRS+=$(wildcard service-private)

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

bof: common
mbbsd: common bof
util: common bof
service: common
service-private: common

$(SUBDIRS):
	$(MAKE) -C $@

clean install:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || exit 1; \
	done
