SUBDIR=	mbbsd util innbbsd
BBSHOME?=$(HOME)

all install clean:
	@for i in $(SUBDIR); do\
		cd $$i;\
		make BBSHOME=$(BBSHOME) OSTYPE=$(OSTYPE) $@;\
		cd ..;\
	done
