SUBDIR=	libbbsutil libbbs

all install clean:
	@for i in $(SUBDIR); do\
		cd $$i;\
		$(MAKE) $@;\
		cd -;\
	done
