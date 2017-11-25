/* munin program acts as a munin plugin providing statistics in shm. */
#include <stdio.h>
#include "bbs.h"

typedef struct {
    const char *name;
    unsigned int shm_index;
    const char *type;
} var_t;

typedef struct {
    const char *name;
    const var_t *vars;
    const char *title;
} module_t;

#define DEFINE_VAR(type, name) {#name, STAT_ ## name, #type}
#define DEFINE_VAR_END {NULL, 0, NULL}

static var_t login_vars[] = {
    DEFINE_VAR(COUNTER, LOGIN),
    DEFINE_VAR(COUNTER, SHELLLOGIN),
    DEFINE_VAR_END,
};

static var_t logind_vars[] = {
    DEFINE_VAR(COUNTER, LOGIND_NEWCONN),
    DEFINE_VAR(COUNTER, LOGIND_OVERLOAD),
    DEFINE_VAR(COUNTER, LOGIND_BANNED),
    DEFINE_VAR(COUNTER, LOGIND_PASSWDPROMPT),
    DEFINE_VAR(COUNTER, LOGIND_AUTHFAIL),
    DEFINE_VAR(COUNTER, LOGIND_SERVSTART),
    DEFINE_VAR(COUNTER, LOGIND_SERVFAIL),
    DEFINE_VAR_END,
};

static var_t syscall_vars[] = {
    DEFINE_VAR(COUNTER, SYSWRITESOCKET),
    DEFINE_VAR(COUNTER, SYSSELECT),
    DEFINE_VAR(COUNTER, SYSREADSOCKET),
    DEFINE_VAR_END,
};

static var_t signal_vars[] = {
    DEFINE_VAR(COUNTER, SIGINT),
    DEFINE_VAR(COUNTER, SIGQUIT),
    DEFINE_VAR(COUNTER, SIGILL),
    DEFINE_VAR(COUNTER, SIGABRT),
    DEFINE_VAR(COUNTER, SIGFPE),
    DEFINE_VAR(COUNTER, SIGBUS),
    DEFINE_VAR(COUNTER, SIGSEGV),
    DEFINE_VAR(COUNTER, SIGXCPU),
    DEFINE_VAR_END,
};

static var_t user_vars[] = {
    DEFINE_VAR(COUNTER, SEARCHUSER),
    DEFINE_VAR(COUNTER, QUERY),
    DEFINE_VAR(COUNTER, FRIENDDESC),
    DEFINE_VAR(COUNTER, FRIENDDESC_FILE),
    DEFINE_VAR(COUNTER, PICKMYFRIEND),
    DEFINE_VAR(COUNTER, PICKBFRIEND),
    DEFINE_VAR_END,
};

static var_t more_vars[] = {
    DEFINE_VAR(COUNTER, MORE),
    DEFINE_VAR(COUNTER, READPOST),
    DEFINE_VAR_END,
};

static var_t thread_vars[] = {
    DEFINE_VAR(COUNTER, THREAD),
    DEFINE_VAR(COUNTER, SELECTREAD),
    DEFINE_VAR_END,
};

static var_t record_vars[] = {
    DEFINE_VAR(COUNTER, BOARDREC),
    DEFINE_VAR_END,
};

static var_t misc_vars[] = {
    DEFINE_VAR(COUNTER, VEDIT),
    DEFINE_VAR(COUNTER, TALKREQUEST),
    DEFINE_VAR(COUNTER, WRITEREQUEST),
    DEFINE_VAR(COUNTER, DOSEND),
    DEFINE_VAR(COUNTER, DOTALK),
    DEFINE_VAR(COUNTER, GAMBLE),
    DEFINE_VAR(COUNTER, DOPOST),
    DEFINE_VAR(COUNTER, RECOMMEND),
    DEFINE_VAR(COUNTER, DORECOMMEND),
    DEFINE_VAR_END,
};

static var_t cpu_vars[] = {
    DEFINE_VAR(COUNTER, BOARDREC_SCPU),
    DEFINE_VAR(COUNTER, BOARDREC_UCPU),
    DEFINE_VAR(COUNTER, DORECOMMEND_SCPU),
    DEFINE_VAR(COUNTER, DORECOMMEND_UCPU),
    DEFINE_VAR(COUNTER, QUERY_SCPU),
    DEFINE_VAR(COUNTER, QUERY_UCPU),
    DEFINE_VAR_END,
};

static var_t mbbsd_vars[] = {
    DEFINE_VAR(COUNTER, MBBSD_ENTER),
    DEFINE_VAR(COUNTER, MBBSD_EXIT),
    DEFINE_VAR(COUNTER, MBBSD_ABORTED),
    DEFINE_VAR_END,
};

static module_t modules[] = {
    {"login",   login_vars,   "bbs login"},
    {"logind",  logind_vars,  "bbs logind"},
    {"syscall", syscall_vars, "bbs syscall"},
    {"signal",  signal_vars,  "bbs signal"},
    {"user",    user_vars,    "bbs user"},
    {"more",    more_vars,    "bbs more"},
    {"thread",  thread_vars,  "bbs thread"},
    {"record",  record_vars,  "bbs record"},
    {"misc",    misc_vars,    "bbs misc"},
    {"cpu",     cpu_vars,     "bbs cpu"},
    {"mbbsd",   mbbsd_vars,   "bbs mbbsd"},
    {NULL, NULL, NULL},
};

// find_module returns the pointer to module of the specified name.
static const module_t *find_module(const char *name) {
    for (int i = 0; modules[i].name; i++) {
	if (!strcmp(modules[i].name, name))
	    return &modules[i];
    }
    return NULL;
}

// config outputs munin plugin config according to the specified module.
static int config(const char *mname) {
    const module_t *m = find_module(mname);
    if (!m)
	return -1;
    printf("graph_title %s\n", m->title);
    printf("graph_category bbs\n");
    printf("graph_order");
    for (int i = 0; m->vars[i].name; i++) {
	printf(" %s", m->vars[i].name);
    }
    printf("\n");
    for (int i = 0; m->vars[i].name; i++) {
	const var_t *v = &m->vars[i];
	printf("%s.name %s\n", v->name, v->name);
	printf("%s.label %s\n", v->name, v->name);
	printf("%s.type %s\n", v->name, v->type);
    }
    return 0;
}

// run outputs munin plugin values of the specified module.
static int run(const char *mname) {
    const module_t *m = find_module(mname);
    if (!m)
	return -1;
    attach_SHM();
    for (int i = 0; m->vars[i].name; i++) {
	const var_t *v = &m->vars[i];
	if (v->shm_index >= STAT_MAX) {
	    fprintf(stderr, "Var '%s' wants statistic[%d], which is greater than STAT_MAX %d.\n", v->name, v->shm_index, STAT_MAX);
	    return 1;
	}
	printf("%s.value %u\n", v->name, SHM->statistic[v->shm_index]);
    }
    return 0;
}

struct {
    const char *action;
    int (*func)(const char *mname);
} actions[] = {
    {"config", config},
    {"run", run},
    {NULL, NULL},
};

int main(int argc, char *argv[]) {
    const char *prog = strrchr(argv[0], '/');
    if (!prog)
	prog = argv[0];
    else
	prog++;

    const char *prefix = "bbs_";
    if (strncmp(prog, prefix, strlen(prefix))) {
	fprintf(stderr, "Program/symlink must be prefixed with '%s'.\n", prefix);
	return 1;
    }

    const char *module = prog + strlen(prefix);
    const char *action;
    switch (argc) {
	case 1:
	    action = "run";
	    break;
	case 2:
	    action = argv[1];
	    break;
	default:
	    fprintf(stderr, "Too many arguments.\n");
	    return 1;
    }

    int i;
    for (i = 0; actions[i].action; i++) {
	if (!strcmp(action, actions[i].action))
	    break;
    }
    if (actions[i].action)
	return actions[i].func(module);
    fprintf(stderr, "Unknown action '%s'.\n", argv[1]);
    return 1;
}
