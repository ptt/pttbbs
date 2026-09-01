SUBDIRS:=	common mbbsd util service

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

mbbsd: common
util: common
service: common

$(SUBDIRS):
	$(MAKE) -C $@

clean install:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || exit 1; \
	done
