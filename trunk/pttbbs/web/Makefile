all:	mod_ptt.c mod_ptt.h
	apxs -I ../include -c mod_ptt.c
	cp mod_ptt.so /usr/local/libexec/apache/mod_ptt.so
	chmod 775 /usr/local/libexec/apache/mod_ptt.so
