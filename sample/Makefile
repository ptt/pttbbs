SUBDIR=etc innd
BBSHOME?=$(HOME)

all:

install:
	@for i in $(SUBDIR); do\
		cd $$i;\
		$(MAKE) BBSHOME=$(BBSHOME) $@;\
		cd ..;\
	done
