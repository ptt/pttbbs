SUBDIR=	mbbsd util innbbsd
BBSHOME?=$(HOME)
OSTYPE!=uname

all install clean:
.if !exists(/usr/local/lib/libhz.so) && !exists(/usr/lib/libhz.so)
	@echo "sorry, libhz not found."
	@echo "above FreeBSD, please install /usr/ports/chinese/autoconvert"
	@echo "above Debian/Linux, please install package libhz0"
	@exit 1
.endif
	@for i in $(SUBDIR); do\
		cd $$i;\
		$(MAKE) BBSHOME=$(BBSHOME) $@;\
		cd ..;\
	done
