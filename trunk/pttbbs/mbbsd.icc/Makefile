.PATH: ../mbbsd

BBSHOME=/home/bbs
PROG=	mbbsd.icc
OBJS=	admin.o announce.o args.o bbs.o board.o cache.o cal.o card.o\
	chat.o chc_draw.o chc_net.o chc_play.o chc_rule.o chicken.o dark.o\
	dice.o edit.o friend.o gamble.o gomo.o gomo1.o guess.o indict.o io.o\
	kaede.o lovepaper.o mail.o mbbsd.o menu.o more.o name.o osdep.o\
	othello.o page.o read.o record.o register.o screen.o stuff.o\
	talk.o term.o topsong.o user.o vice.o vote.o xyz.o\
	voteboard.o syspost.o var.o toolkit.o passwd.o\
	calendar.o

CFLAGS= -DHAVE_SETPROCTITLE -DBBSHOME='"$(BBSHOME)"' -DFreeBSD -I../include
LDFLAGS= -lutil -lkvm -lcrypt

.if defined(GDB) || defined(DEBUG)
CFLAGS+= -g
FDFLAGS+= -g
.else
CFLAGS+= -O
LDFLAGS+= -O
.endif

.if defined(DEBUG)
CFLAGS+= -DDEBUG
LDFLAGS+= -DDEBUG
.endif

.if defined(NO_FORK)
CFLAGS+= -DNO_FORK
.endif

CFLAGS+= "-DCOMPILE_TIME=\"`date`\""
GCCUSE=gcc -pipe -Wall $(CFLAGS) -c

.SUFFIXES: .c .o
.c.o:	../include/var.h
	iccbin -D__FreeBSD__ -D__FreeBSD $(CFLAGS) -c ../mbbsd/$*.c

all: $(PROG)

$(PROG): $(OBJS) 
	cc $(LDFLAGS) -o $(PROG) $(OBJS) 

../include/var.h:	var.c
	perl ../util/parsevar.pl < ../mbbsd/var.c > ../include/var.h

install: $(PROG)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROG) $(BBSHOME)/bin/
	rm -f $(BBSHOME)/bin/mbbsd
	ln -sv $(BBSHOME)/bin/$(PROG) $(BBSHOME)/bin/mbbsd
cache.o: cache.c
	$(GCCUSE) ../mbbsd/$*.c

mail.o: mail.c
	$(GCCUSE) ../mbbsd/$*.c

menu.o: menu.c
	$(GCCUSE) ../mbbsd/$*.c

record.o: record.c
	$(GCCUSE) ../mbbsd/$*.c

topsong.o: topsong.c
	$(GCCUSE) ../mbbsd/$*.c

xyz.o: xyz.c
	$(GCCUSE) ../mbbsd/$*.c
 
test: $(PROG)
	killall -9 testmbbsd || true
	cp $(PROG) testmbbsd
	./testmbbsd 9000
	rm -f testmbbsd

clean:
	rm -f $(OBJS) $(PROG)
