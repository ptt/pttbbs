/* $Id: args.c,v 1.3 2002/07/05 17:10:26 in2 Exp $ */
#include "bbs.h"
#ifdef HAVE_SETPROCTITLE

void 
initsetproctitle(int argc, char **argv, char **envp)
{
}

#else


char          **Argv = NULL;	/* pointer to argument vector */
char           *LastArgv = NULL;/* end of argv */
extern char   **environ;

void 
initsetproctitle(int argc, char **argv, char **envp)
{
    register int    i;

    /*
     * Move the environment so setproctitle can use the space at the top of
     * memory.
     */
    for (i = 0; envp[i]; i++);
    environ = malloc(sizeof(char *) * (i + 1));
    for (i = 0; envp[i]; i++)
	environ[i] = strdup(envp[i]);
    environ[i] = NULL;

    /* Save start and extent of argv for setproctitle. */
    Argv = argv;
    if (i > 0)
	LastArgv = envp[i - 1] + strlen(envp[i - 1]);
    else
	LastArgv = argv[argc - 1] + strlen(argv[argc - 1]);
}

static void 
do_setproctitle(const char *cmdline)
{
    char            buf[256], *p;
    int             i;

    strncpy(buf, cmdline, 256);
    buf[255] = '\0';
    i = strlen(buf);
    if (i > LastArgv - Argv[0] - 2) {
	i = LastArgv - Argv[0] - 2;
    }
    strcpy(Argv[0], buf);
    p = &Argv[0][i];
    while (p < LastArgv)
	*p++ = '\0';
    Argv[1] = NULL;
}

void 
setproctitle(const char *format,...)
{
    char            buf[256];
    va_list         args;
    va_start(args, format);
    vsprintf(buf, format, args);
    do_setproctitle(buf);
    va_end(args);
}
#endif
