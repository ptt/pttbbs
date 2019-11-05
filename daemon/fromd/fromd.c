// $Id$
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <event.h>

#ifdef FROMD_USE_MAXMIND_DB
#include <maxminddb.h>
static MMDB_s *g_mmdb;
static const char *mmdb_file = "/var/lib/GeoIP/GeoLite2-Country.mmdb";
#endif

#ifdef FROMD_USE_GEOIP_DB
#include <GeoIP.h>
static GeoIP *g_geoip;
#endif

#include "bbs.h"
#include "daemons.h"
#include "country_names.h"
#include "ip_desc_db.h"

static const char * cfgfile = BBSHOME "/etc/domain_name_query.cidr";
static struct timeval tv = {5, 0};
static struct event ev_listen, ev_sighup;

#ifdef FROMD_USE_MAXMIND_DB
/**
 * Wrapper to MMDB_open that allocates handle on heap.
 *
 * @return: Returns pointer to MMDB_s handle or NULL on error.
 */
static MMDB_s *my_MMDB_open()
{
    MMDB_s *mmdb;
    int ret;

    mmdb = calloc(sizeof(*mmdb), 1);
    if (!mmdb)
	return NULL;

    ret = MMDB_open(mmdb_file, 0, mmdb);
    if (ret != MMDB_SUCCESS) {
	free(mmdb);
	return NULL;
    }

    return mmdb;
}

/**
 * Wrapper to MMDB_close that frees handle on heap.
 *
 * @mmdb: pointer to MMDB_s handle, from my_MMDB_open()
 */
static void my_MMDB_close(MMDB_s *mmdb)
{
    if (mmdb)
	MMDB_close(mmdb);
    free(mmdb);
}

static const char *my_MMDB_lookup(MMDB_s *mmdb, const char *str)
{
    MMDB_lookup_result_s mmdb_res;
    MMDB_entry_data_s entry_data;
    int gai_error, mmdb_error;
    char buf[3];

    mmdb_res = MMDB_lookup_string(mmdb, str, &gai_error, &mmdb_error);
    if (gai_error < 0)
	return NULL;
    if (mmdb_error != MMDB_SUCCESS)
	return NULL;
    if (!mmdb_res.found_entry)
	return NULL;

    mmdb_error = MMDB_get_value(&mmdb_res.entry, &entry_data,
				"registered_country", "iso_code", NULL);
    if (mmdb_error != MMDB_SUCCESS)
	return NULL;
    if (!entry_data.has_data)
	return NULL;

    /* Expect country code to be 2 byte UTF-8 string (not null-terminated) */
    if (entry_data.type != MMDB_DATA_TYPE_UTF8_STRING ||
	entry_data.data_size != 2)
	return NULL;

    /* Convert result to null-terminated string */
    memcpy(buf, entry_data.utf8_string, entry_data.data_size);
    buf[2] = '\0';

    return country_code_to_name(buf);
}
#endif

static void sighup_cb(int signal, short event, void *arg)
{
    ip_desc_db_reload(cfgfile);

#ifdef FROMD_USE_MAXMIND_DB
    MMDB_s *mmdb = my_MMDB_open();
    if (mmdb) {
	/* Replace global MMDB handle */
	MMDB_s *old_mmdb = g_mmdb;
	g_mmdb = mmdb;

	/* Clean up old MMDB handle */
	my_MMDB_close(old_mmdb);
    }
#endif
}

static void client_cb(int fd, short event, void *arg)
{
    char buf[32];
    const char *result, *cc = NULL;
    int len;

    // ignore clients that timeout
    if (event & EV_TIMEOUT)
	goto end;

    if ( (len = read(fd, buf, sizeof(buf) - 1)) <= 0 )
	goto end;

    buf[len] = '\0';

    result = ip_desc_db_lookup(buf);

#ifdef FROMD_USE_MAXMIND_DB
    if (g_mmdb && !cc)
	cc = my_MMDB_lookup(g_mmdb, buf);
#endif

#ifdef FROMD_USE_GEOIP_DB
    if (g_geoip && !cc) {
	cc = GeoIP_country_code_by_addr(g_geoip, buf);
	if (cc)
	    cc = country_code_to_name(cc);
    }
#endif

    if (cc && !strcmp(result, buf))
	result = cc;

    if (!cc)
	result = cc = "¤£©ú";

    {
	// This needs to be one single write syscall, otherwise the client
	// might not pick up the second part.
	struct iovec iov[] = {
	    { .iov_base = (void *)result, .iov_len = strlen(result) },
	    { .iov_base = "\n", .iov_len = 1 },
	    { .iov_base = (void *)cc, .iov_len = strlen(cc) },
	};

	writev(fd, iov, cc ? 3 : 1);
    }

end:
    // cleanup
    close(fd);
    free(arg);
}

static void listen_cb(int fd, short event, void *arg)
{
    int cfd;

    if ((cfd = accept(fd, NULL, NULL)) < 0 )
	return;

    struct event *ev = malloc(sizeof(struct event));

    event_set(ev, cfd, EV_READ, client_cb, ev);
    event_add(ev, &tv);
}

int main(int argc, char *argv[])
{
    int     ch, sfd;
    char   *iface_ip = FROMD_ADDR;

    Signal(SIGPIPE, SIG_IGN);

    while ( (ch = getopt(argc, argv, "m:i:h")) != -1 )
	switch( ch ){
	case 'i':
	    iface_ip = optarg;
	    break;
#ifdef FROMD_USE_MAXMIND_DB
	case 'm':
	    mmdb_file = optarg;
	    break;
#endif
	case 'h':
	default:
	    fprintf(stderr, "usage: %s [-m MaxMindDB path] [-i [interface_ip]:port]\n", argv[0]);
	    return 1;
	}

    if ( (sfd = tobind(iface_ip)) < 0 )
	return 1;

    daemonize(BBSHOME "/run/fromd.pid", NULL);

    ip_desc_db_reload(cfgfile);

#ifdef FROMD_USE_MAXMIND_DB
    g_mmdb = my_MMDB_open();
    if (!g_mmdb)
	fprintf(stderr, "Unable to load MaxmindDB, continuing without it.\n");
#endif

#ifdef FROMD_USE_GEOIP_DB
    g_geoip = GeoIP_new(GEOIP_MEMORY_CACHE | GEOIP_CHECK_CACHE);
    if (!g_geoip)
	fprintf(stderr, "Unable to load geo ip, continuing without it.\n");
#endif

    event_init();
    event_set(&ev_listen, sfd, EV_READ | EV_PERSIST, listen_cb, &ev_listen);
    event_add(&ev_listen, NULL);
    signal_set(&ev_sighup, SIGHUP, sighup_cb, &ev_sighup);
    signal_add(&ev_sighup, NULL);
    event_dispatch();

#ifdef FROMD_USE_GEOIP_DB
    GeoIP_cleanup();
#endif

    return 0;
}
