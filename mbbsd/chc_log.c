#include "bbs.h"

extern char * chc_getstep(board_t board);

static FILE *fp = NULL;

int
chc_log_open(chcusr_t *user1, chcusr_t *user2, char *file)
{
    char buf[128];
    if ((fp = fopen(file, "w")) == NULL)
	return -1;
    sprintf(buf, "%s V.S. %s\n", user1->userid, user2->userid);
    fputs(buf, fp);
    return 0;
}

void
chc_log_close(void)
{
    if (fp)
	fclose(fp);
}

int
chc_log(char *desc)
{
    if (fp)
	return fputs(desc, fp);
    return -1;
}

int
chc_log_step(board_t board, rc_t *from, rc_t *to)
{
    char buf[80];
    sprintf(buf, "  %s%s\033[m\n", CHE_O(board[from->r][from->c]) == 0 ? BLACK_COLOR : RED_COLOR, getstep(board, from, to));
    return chc_log(buf);
}

static int
chc_filter(struct dirent *dir)
{
    if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0 )
	return 0;
    return strstr(dir->d_name, ".poem") != NULL;
}

int
chc_log_poem(void)
{
    struct dirent **namelist;
    int n;

    n = scandir(BBSHOME"/etc/chess", &namelist, chc_filter, alphasort);
    if (n < 0)
	perror("scandir");
    else {
	char buf[80];
	FILE *fp;
	sprintf(buf, BBSHOME"/etc/chess/%s", namelist[rand() % n]->d_name);
	if ((fp = fopen(buf, "r")) == NULL)
	    return -1;

	while(fgets(buf, sizeof(buf), fp) != NULL)
	    chc_log(buf);
	while(n--)
	    free(namelist[n]);
	free(namelist);
	fclose(fp);
    }
    return 0;
}
