#include <stdio.h>
#include <stdlib.h>
#include <sys/dkstat.h>
#include <devstat.h>
#include <err.h>

int main(int argc, char **argv)
{
    return 0;
}

#if 0
typedef enum {
	DS_MATCHTYPE_NONE,
	DS_MATCHTYPE_SPEC,
	DS_MATCHTYPE_PATTERN
} last_match_type;
last_match_type last_type;
struct device_selection *dev_select;
long generation;
int num_devices, num_selected;
int num_selections;
long select_generation;
struct devstat_match *matches = NULL;
int num_matches = 0;
char **specified_devices;
int num_devices_specified = 0;

int
dsinit(int maxshowdevs, struct statinfo *s1, struct statinfo *s2,
       struct statinfo *s3)
{
	generation = 0;
	num_devices = 0;
	num_selected = 0;
	num_selections = 0;
	select_generation = 0;
	last_type = DS_MATCHTYPE_NONE;

	if (getdevs(s1) == -1)
		errx(1, "%s", devstat_errbuf);

	num_devices = s1->dinfo->numdevs;
	generation = s1->dinfo->generation;

	dev_select = NULL;

	/*
	 * At this point, selectdevs will almost surely indicate that the
	 * device list has changed, so we don't look for return values of 0
	 * or 1.  If we get back -1, though, there is an error.
	 */
	if (selectdevs(&dev_select, &num_selected, &num_selections,
		       &select_generation, generation, s1->dinfo->devices,
		       num_devices, NULL, 0, NULL, 0, DS_SELECT_ADD,
		       maxshowdevs, 0) == -1)
		errx(1, "%s", devstat_errbuf);

	return(1);
}

static void
dinfo(dn, lc, now, then)
     int dn, lc;
     struct statinfo *now, *then;
{
    long double transfers_per_second;
    long double kb_per_transfer, mb_per_second;
    long double elapsed_time, device_busy;
    int di;

    di = dev_select[dn].position;

    elapsed_time = devstat_compute_etime(now->busy_time,
	 then ? then->busy_time : now->dinfo->devices[di].dev_creation_time);

    device_busy =  devstat_compute_etime(now->dinfo->devices[di].busy_time,
			 then ? then->dinfo->devices[di].busy_time :
			 now->dinfo->devices[di].dev_creation_time);

    if ((device_busy == 0) && (transfers_per_second > 5))
	/* the device has been 100% busy, fake it because               
	 * as long as the device is 100% busy the busy_time             
	 * field in the devstat struct is not updated */
	device_busy = elapsed_time;
    if (device_busy > elapsed_time)
	/* this normally happens after one or more periods              
	 * where the device has been 100% busy, correct it */
	device_busy = elapsed_time;

    /*
    putlongdouble(kb_per_transfer, DISKROW + 1, lc, 5, 2, 0);
    putlongdouble(transfers_per_second, DISKROW + 2, lc, 5, 0, 0);
    putlongdouble(mb_per_second, DISKROW + 3, lc, 5, 2, 0);
    putlongdouble(device_busy * 100 / elapsed_time, DISKROW + 4, lc, 5, 0,  0);
    */
}

#endif
