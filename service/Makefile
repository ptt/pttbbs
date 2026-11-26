SUBDIRS:=	friend ipc mail gem nest rotatelog search stat utmp

.PHONY: all clean install test $(SUBDIRS)

all clean install test: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)
