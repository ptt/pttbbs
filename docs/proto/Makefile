
#.SUFFIXES: .c .txt

# XXX 這有沒有比較好的作法?
# 直接用 .c.txt 好像會先去看目前目錄有沒有 xxx.c @_@

NAME!=	cd ../../mbbsd/ && ls *.c | sed -e "s/\.c//"
SRC!=	cd ../../mbbsd/ && ls *.c | sed -e "s/\.c/.txt/"

all: $(SRC)

.for fn in $(NAME)
$(fn).txt: ../../mbbsd/$(fn).c
	./cdoc ../../mbbsd/$(fn).c > $(fn).txt
.endfor

clean:
	rm -f *.txt
