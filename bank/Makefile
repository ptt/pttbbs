
CFLAGS = -O2 -Os -fomit-frame-pointer -fstrength-reduce  -fthread-jumps -fexpensive-optimizations -pipe -I../include -DLinux -DBANK_ONLY
#CFLAGS = -O2 -Os -fomit-frame-pointer -fstrength-reduce  -fthread-jumps -fexpensive-optimizations -Wall -pipe -I../include -DLinux -DBANK_ONLY

.if defined(DEBUG)
CFLAGS += -g
.endif

.if !defined(NOTPTT)
CFLAGS += -DPTTBBS
.endif

SERVEROBJ = server.o bankstuff.o money.o
CLIENTOBJ = client.o bankstuff.o
EXTFILE = cache passwd stuff
EXTOBJ = mbbsd_cache.o mbbsd_passwd.o mbbsd_stuff.o
PROG = client server

.SUFFIXES: .c .o
.c.o: ; $(CC) $(CFLAGS) -c $*.c

all: $(PROG)

.if !defined(NOTPTT)

.for fn in $(EXTFILE)
mbbsd_$(fn).o: ../mbbsd/$(fn).c
	$(CC) $(CFLAGS) -o mbbsd_$(fn).o -c ../mbbsd/$(fn).c
.endfor

.else
# define your rules here.

.endif

server: $(SERVEROBJ) $(EXTOBJ)
	$(CC) $(CFLAGS) -o server $(SERVEROBJ) $(EXTOBJ)

client: $(CLIENTOBJ)
	$(CC) $(CFLAGS) -o client $(CLIENTOBJ)

clean:
	rm -rf *.o $(PROG)
