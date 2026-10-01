SUBDIRS:=	friend search utmp

.PHONY: all clean install test $(SUBDIRS)

all clean install test: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)
