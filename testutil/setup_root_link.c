#include <libgen.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SETUP_ROOT_LINK_MAX_BUFFER 8192

int setup_root_link(const char *bbshome) {
  char filename_for_basename[SETUP_ROOT_LINK_MAX_BUFFER];
  char filename_for_dirname[SETUP_ROOT_LINK_MAX_BUFFER];
  fprintf(stderr, "[setup_root_link] start: bbshome: %s\n", bbshome);
  strcpy(filename_for_basename, bbshome);
  strcpy(filename_for_dirname, bbshome);

  char *the_basename = basename(filename_for_basename);
  fprintf(stderr, "[setup_root_link] the_basename: %s bbshome: %s\n", the_basename, bbshome);

  char *the_dirname = dirname(filename_for_dirname);
  fprintf(stderr, "[setup_root_link] the_dirname: %s\n", the_dirname);

  char cwd[SETUP_ROOT_LINK_MAX_BUFFER];
  char cmd[SETUP_ROOT_LINK_MAX_BUFFER];

  int is_err = 0;
  int err = 0;

  if(getcwd(cwd, SETUP_ROOT_LINK_MAX_BUFFER) == NULL) {
    fprintf(stderr, "[setup_root_link] unable to getcwd\n");
    return -1;
  }

  sprintf(cmd, "mkdir -p %s", the_dirname);
  system(cmd);
  chdir(the_dirname);
  is_err = symlink(cwd, the_basename);
  if(is_err) {
    err = errno;
  }
  chdir(cwd);
  return 0;
}
