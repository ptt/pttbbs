SUBDIRS:=	friend ipc mail post search stat utmp

.PHONY: all clean install test $(SUBDIRS)

all clean install test: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)
