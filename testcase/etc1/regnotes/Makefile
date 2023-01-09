BBSHOME?=$(HOME)
TARGET=$(BBSHOME)/etc/regnotes/
INSTALLTAG=$(TARGET).installed
FILES=	address birthday career front mobile name phone sex

all:

install:
	install -d $(TARGET)
	test ! -e $(INSTALLTAG) && touch $(INSTALLTAG)
	install -c -m 644 $(FILES) $(TARGET)

