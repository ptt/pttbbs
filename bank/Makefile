
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
EXTOBJ = cache.o passwd.o stuff.o
PROG = client server

.SUFFIXES: .c .o
.c.o: ; $(CC) $(CFLAGS) -c $*.c

all: $(PROG)

.if !defined(NOTPTT)

cache: ../mbbsd/cache.c
	$(CC) $(CFLAGS) -c ../mbbsd/cache.c

passwd: ../mbbsd/passwd.c
	$(CC) $(CFLAGS) -c ../mbbsd/passwd.c

stuff: ../mbbsd/stuff.c
	$(CC) $(CFLAGS) -c ../mbbsd/stuff.c

.else
# define your rules here.

.endif

server: $(SERVEROBJ) $(EXTFILE)
	$(CC) $(CFLAGS) -o server $(SERVEROBJ) $(EXTOBJ)

client: $(CLIENTOBJ)
	$(CC) $(CFLAGS) -o client $(CLIENTOBJ)

clean:
	rm -rf *.o $(PROG)
