SUBDIR=	chess chickens
BBSHOME?=$(HOME)
TARGET=$(BBSHOME)/etc/
INSTALLTAG=$(TARGET).installed
FILES=	@five		Welcome		Welcome_birth	Welcome_login	\
	Logout		goodbye		bad_host 	today_boring	\
	register	registered	registermail	registeredmail 	\
	feast		sysop		\
	ve.hlp		board.help	boardlist.help	\
	editable	expire.conf	domain_name_query.cidr	\
	banemail	whitemail	whitemail.notice

all:

install_sub:
	@for i in $(SUBDIR); do\
		cd $$i;\
		$(MAKE) BBSHOME=$(BBSHOME) install;\
		cd ..;\
	done

install: install_sub
	install -d $(TARGET)
	test ! -e $(INSTALLTAG) && touch $(INSTALLTAG)
	install -c -m 644 $(FILES) $(TARGET)
