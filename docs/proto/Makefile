
#.SUFFIXES: .c .txt

# XXX 這有沒有比較好的作法?
# 直接用 .c.txt 好像會先去看目前目錄有沒有 xxx.c @_@

NAME:=	$(shell cd ../../mbbsd/ && ls *.c | sed -e "s/\.c//")
SRC:=	$(NAME:=.txt)

.PHONY: all clean

all: $(SRC)

%.txt: ../../mbbsd/%.c
	./cdoc $< > $@

clean:
	rm -f *.txt
