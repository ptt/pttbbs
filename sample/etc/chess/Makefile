BBSHOME?=$(HOME)
TARGET=$(BBSHOME)/etc/chess/
INSTALLTAG=$(TARGET).installed
FILES=	1.poem 2.poem 3.poem 4.poem 5.poem 6.poem 7.poem 8.poem 9.poem \
	10.poem 11.poem 12.poem 13.poem 14.poem 15.poem 16.poem 

all:

install:
	install -d $(TARGET)
	test ! -e $(INSTALLTAG) && touch $(INSTALLTAG)
	install -c -m 644 $(FILES) $(TARGET)

