SUBDIR=	chess chickens
BBSHOME?=$(HOME)
TARGET=$(BBSHOME)/etc/
FILES=	@five		Welcome		register	today_boring	\
	Welcome_birth	domain_name_query.cidr		registered	\
	ve.hlp		Logout		Welcome_login	expire.conf	\
	registeredmail	MRT.map		bad_host	feast		\
	registermail	Makefile	banemail	goodbye		\
	sysop

all:

install_sub:
	@for i in $(SUBDIR); do\
		cd $$i;\
		$(MAKE) BBSHOME=$(BBSHOME) install;\
		cd ..;\
	done

install: install_sub
	install -d $(TARGET)
	install -c -m 644 $(FILES) $(TARGET)
