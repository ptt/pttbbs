all:	mod_ptt.c mod_ptt.h
	apxs -I ../include -c mod_ptt.c util_cache.c util_passwd.c util_record.c
	cp mod_ptt.so /usr/local/libexec/apache/mod_ptt.so
	chmod 775 /usr/local/libexec/apache/mod_ptt.so
clean:
	rm -f *.o *.a *.so
