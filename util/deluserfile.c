/* 自動砍user目錄檔案程式 */

#include "bbs.h"

#define HOLDWRITELOG
#define DELZEROFILE

void del_file(char *userid)
{
    char user_home[PATHLEN];
    struct dirent *de;
    DIR *dirp;

    sethomepath(user_home, userid);

    if (!(dirp = opendir(user_home)))
	return;

    while ((de = readdir(dirp)))
    {
	char *fname = de->d_name;
	if (fname[0] > ' ' && fname[0] != '.')
	{
	    char user_file[PATHLEN];
	    sethomefile(user_file, userid, fname);
	    if (strstr(fname, "writelog"))
#ifdef HOLDWRITELOG
	    {
		char log_filename[PATHLEN];

		sethomefile(log_filename, userid, "writelog");
		if (deliver_home_mail_file(userid, "[備.忘.錄]", "熱線記錄",
					   log_filename, FILE_READ) == 0)
		    unlink(log_filename);
	    }
#else
	    unlink(user_file);
#endif
	    else if (strstr(fname, "chat_"))
		unlink(user_file);
	    else if (strstr(fname, "ve_"))
		unlink(user_file);
	    else if (strstr(fname, "SR."))
		unlink(user_file);
	    else if (strstr(fname, ".old"))
		unlink(user_file);
	    else if (strstr(fname, "talk_"))
		unlink(user_file);
	}
    }
    closedir(dirp);
}

void mv_user_home(char *userid)
{
    char user_home[PATHLEN];

    printf("move user %s to tmp\n", userid);
    sethomepath(user_home, userid);
    AUTO_STR(command, "cp -R %s tmp", user_home);
    if (!system(command))
    {				//Copy success

	RmTree(user_home);
    }
}

int main(void)
{
    struct dirent *de;
    DIR *dirp;
    char *userid, ch;
    int count = 0;
/* visit all users  */

    printf("new version, deleting\n");

    attach_SHM();
    chdir(BBSHOME);

    for (ch = 'A'; ch <= 'z'; ch++)
    {
	if(ch > 'Z' && ch < 'a')
	    continue;
	printf("Cleaning %c\n", ch);
	AUTO_STR(homedir, BBSHOME "/home/%c", ch);
	if (!(dirp = opendir(homedir)))
	{
	    printf("unable to open %s\n", homedir);
	    continue;
	}

	while ((de = readdir(dirp)))
	{
	    userid = de->d_name;

	    /* 預防錯誤 */
	    if (is_validuserid(userid))
	    {
		if (!(count++ % 300))
		    printf(".\n");

		if (!searchuser(userid, NULL)) {
		    // user doesn't exist
		    mv_user_home(userid);
		} else
		    del_file(userid);
	    }
	}
	closedir(dirp);
    }
    return 0;
}
