SUBDIR=etc innd
BBSHOME?=$(HOME)

all:

install:
	@for i in $(SUBDIR); do\
		cd $$i;\
		make BBSHOME=$(BBSHOME) OSTYPE=$(OSTYPE) $@;\
		cd ..;\
	done
