#include "bbs.h"

static int run()
{
    int     i, i60, i300, i600, total;
    time_t  now, t60, t300, t600, maxidle_time;

    i60 = i300 = i600 = total = 0;
    now = time(NULL); t60 = now - 60; t300 = now - 300; t600 = now - 600;
    maxidle_time = now;

    attach_SHM();
    for (i = 0; i < USHM_SIZE; ++i) {
	if (SHM->uinfo[i].pid) {
	    time_t lastact = time4_to_time(SHM->uinfo[i].lastact);

	    ++total;
	    if (lastact >= t60) {
		++i60; ++i300; ++i600;
	    } else if (lastact >= t300) {
		++i300; ++i600;
	    } else if (lastact >= t600) {
		++i600;
	    } else if (lastact < maxidle_time) {
		maxidle_time = lastact;
	    }
	}
    }
    int64_t maxidle64 = now - maxidle_time;
    int maxidle = maxidle64 > INT_MAX ? INT_MAX : (int)maxidle64;

    printf("multigraph bbs_maxidle\n");
    printf("maxidle.value %d\n", maxidle);

    printf("multigraph bbs_useractive\n");
    printf("total.value %d\n", total);
    printf("i60.value %d\n", i60);
    printf("i300.value %d\n", i300);
    printf("i600.value %d\n", i600);

    return 0;
}

static int config()
{
    printf("multigraph bbs_maxidle\n"
	    "graph_title bbs maxidle\n"
	    "graph_category bbs\n"
	    "graph_order maxidle\n"
	    "maxidle.name maxidle\n"
	    "maxidle.label maxidle\n"
	    "maxidle.type GAUGE\n");
    printf("multigraph bbs_useractive\n"
	    "graph_title bbs useractive\n"
	    "graph_category bbs\n");
    static const char *useractive_names[] = {
	"total", "i60", "i300", "i600", NULL
    };
    printf("graph_order");
    for (const char **p = useractive_names; *p; p++)
	printf(" %s", *p);
    printf("\n");
    for (const char **p = useractive_names; *p; p++)
	printf("%s.name %s\n"
	    "%s.label %s\n"
	    "%s.type GAUGE\n",
	    *p, *p, *p, *p, *p);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "config") == 0)
	return config();
    return run();
}
