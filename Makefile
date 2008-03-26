SUBDIR=	common mbbsd util innbbsd

all install clean:
.if !exists(/usr/local/lib/libhz.so) && !exists(/usr/lib/libhz.so)
	@echo "sorry, libhz not found."
	@echo "above FreeBSD, please install /usr/ports/chinese/autoconvert"
	@echo "above Debian/Linux, please install package libhz0"
	@exit 1
.endif
	@for i in $(SUBDIR); do\
		cd $$i;\
		$(MAKE) $@;\
		cd -;\
	done
