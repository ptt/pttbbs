/* $Id$ */
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
    int len=0,nenv=0;

    /*
     * Move the environment so setproctitle can use the space at the top of
     * memory.
     */
    for (i = 0; envp[i]; i++)
      len+=strlen(envp[i])+1;
    nenv=i+1;
    len+=sizeof(char*)*nenv;
    environ = malloc(len);
    len=0;
    for (i = 0; envp[i]; i++) {
        environ[i] = (char*)environ+nenv*sizeof(char*)+len;
	strcpy(environ[i], envp[i]);
	len+=strlen(envp[i])+1;
    }
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
    int             len;

    len = strlen(cmdline) + 1; // +1 for '\0'
    if(len > LastArgv - Argv[0] - 2) // 2 ??
        len = LastArgv - Argv[0] - 2;
    memset(Argv[0], 0, LastArgv-Argv[0]);
    strlcpy(Argv[0], cmdline, len);
    Argv[1] = NULL;
}

void
setproctitle(const char *format,...)
{
    char            buf[256];
    va_list         args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    do_setproctitle(buf);
    va_end(args);
}
#endif
