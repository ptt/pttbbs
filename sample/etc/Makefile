SUBDIR=	chess chickens
BBSHOME?=$(HOME)
TARGET=$(BBSHOME)/etc/
INSTALLTAG=$(TARGET).installed
FILES=	Welcome		Welcome_login	\
	Logout		goodbye		bad_host 	today_boring	\
	register	registered	registermail	registeredmail 	\
	reg.methods	reg.sms.notes \
	feast		sysop		banip.conf	\
	ve.hlp		board.help	boardlist.help	\
	editable	expire2.conf	domain_name_query.cidr	\
	passwdchanged	emailchanged	\
	banemail	whitemail	whitemail.notice ziphome.exclude

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
