BBSHOME?=$(HOME)
TARGET=$(BBSHOME)/innd/
INSTALLTAG=$(TARGET).installed
FILES=bbsname.bbs newsfeeds.bbs nodelist.bbs ntu.active ncmperm.bbs

all:

install:
	install -d $(TARGET)
	test ! -e $(INSTALLTAG) && touch $(INSTALLTAG)
	install -c -m 644 $(FILES) $(TARGET)
