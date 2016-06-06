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
		fileheader_t mymail;
		char new_filename[PATHLEN];
		char log_filename[PATHLEN];
		char dir_filename[PATHLEN];

		strcpy(new_filename, user_home);
		stampfile(new_filename, &mymail);
		mymail.filemode = FILE_READ;
		strcpy(mymail.owner, "[備.忘.錄]");
		strcpy(mymail.title, "熱線記錄");

		sethomefile(log_filename, userid, "writelog");
		rename(log_filename, new_filename);
		sethomedir(dir_filename, userid);
		append_record(dir_filename, &mymail, sizeof(mymail));
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
    char command[PATHLEN*2];
    char user_home[PATHLEN];

    printf("move user %s to tmp\n", userid);
    sethomepath(user_home, userid);
    sprintf(command, "cp -R %s tmp", user_home);
    if (!system(command))
    {				//Copy success

	sprintf(command, "rm -rf %s", user_home);
	system(command);
    }
}

int main(void)
{
    struct dirent *de;
    DIR *dirp;
    char *userid, buf[200], ch;
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
	sprintf(buf, BBSHOME "/home/%c", ch);
	if (!(dirp = opendir(buf)))
	{
	    printf("unable to open %s\n", buf);
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
