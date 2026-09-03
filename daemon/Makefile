SRCROOT ?= ..
include $(SRCROOT)/pttbbs.mk

# Base daemons always built
SUBDIRS := logind

# Conditional daemons based on pttbbs.conf
ifneq ($(call DEF_CHECK,PLAY_ANGEL),)
SUBDIRS += angelbeats
endif

ifneq ($(call DEF_CHECK,FROMD),)
SUBDIRS += fromd
endif

ifneq ($(call DEF_CHECK,UTMPD),)
SUBDIRS += utmpd
endif

ifneq ($(call DEF_CHECK,QUERY_ARTICLE_URL),)
SUBDIRS += mand boardd
endif

ifneq ($(strip $(call DEF_CHECK,USE_EMAILDB)$(call DEF_CHECK,USE_REGCHECKD)),)
SUBDIRS += regmaild
endif

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean install:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || exit 1; \
	done
