// $Id$
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "bbs.h"

typedef struct {
    uint32_t network, netmask;
    char desc[32];
} db_entry;

static db_entry *db = NULL;
static int db_len = 0, db_size = 0;

#define DB_INCREMENT 200

int ip_desc_db_reload(const char * cfgfile)
{
    FILE *fp;
    char buf[256];
    char *ip, *mask, *strtok_p;
    db_entry *new_db = NULL;
    int new_db_len = 0, new_db_size = 0;
    int result = 0;

    if ( (fp = fopen(cfgfile, "r")) == NULL)
	return -1;

    while (fgets(buf, sizeof(buf), fp)) {
	//skip empty lines
	if (!buf[0] || buf[0] == '\n')
	    continue;

	ip = strtok_r(buf, " \t\n", &strtok_p);

	if (ip == NULL || *ip == '#' || *ip == '@')
	    continue;

	// expand database if nessecary
	if (new_db_len == new_db_size) {
	    void * ptr;
	    if ( (ptr = realloc(new_db, sizeof(db_entry) *
			    (new_db_size + DB_INCREMENT))) == NULL ) {
		result = 1;
		break;
	    }

	    new_db = ptr;
	    new_db_size += DB_INCREMENT;
	}

	// netmask
	if ( (mask = strchr(ip, '/')) != NULL ) {
	    int shift = 32 - atoi(mask + 1);
	    new_db[new_db_len].netmask = htonl(0xFFFFFFFFu << shift);
	    *mask = '\0';
	}
	else
	    new_db[new_db_len].netmask = 0xFFFFFFFFu;

	// network
	new_db[new_db_len].network = htonl(ipstr2int(ip)) &
	    new_db[new_db_len].netmask;

	// description
	if ( (ip = strtok_r(NULL, " \t\n", &strtok_p)) == NULL ) {
	    strcpy(new_db[new_db_len].desc, "雲深不知處");
	}
	else {
	    strlcpy(new_db[new_db_len].desc, ip,
		    sizeof(new_db[new_db_len].desc));
	}

	new_db_len++;
    }

    fclose(fp);

    if (new_db != NULL) {
	free(db);
	db = new_db;
	db_len = new_db_len;
	db_size = new_db_size;
    }

    return result;
}

const char * ip_desc_db_lookup(const char * ip)
{
    int i;
    uint32_t ipaddr = htonl(ipstr2int(ip));

    for (i = 0; i < db_len; i++) {
	if (db[i].network == (ipaddr & db[i].netmask)) {
	    return db[i].desc;
	}
    }
    return ip;
}
