SUBDIR=	mbbsd util innbbsd
BBSHOME?=$(HOME)
OSTYPE!=uname

all install clean:
	@for i in $(SUBDIR); do\
		cd $$i;\
		$(MAKE) BBSHOME=$(BBSHOME) $@;\
		cd ..;\
	done
