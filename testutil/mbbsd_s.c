#include "bbs.h"

static userinfo_t*
getotherlogin(int num)
{
    userinfo_t *ui;
    do {
	if (!(ui = (userinfo_t *) search_ulistn(usernum, num)))
	    return NULL;		/* user isn't logged in */

	/* skip sleeping process, this is slow if lots */
	if(ui->mode == DEBUGSLEEPING)
	    num++;
	else if(ui->pid <= 0)
	    num++;
	else if(kill(ui->pid, 0) < 0)
	    num++;
	else
	    break;
    } while (1);

    return ui;
}

static void
multi_user_check(void)
{
    userinfo_t *ui;
    char            genbuf[3];

    if (HasUserPerm(PERM_SYSOP))
	return;			/* don't check sysops */

    // race condition here, sleep may help..?
    if (cuser.userlevel) {
	usleep(random()%1000000); // 0~1s
	ui = getotherlogin(1);
	if(ui == NULL)
	    return;

	move(b_lines-3, 0); clrtobot();
	outs("\n" ANSI_COLOR(1) "注意: 您有其它連線已登入此帳號。" ANSI_RESET);
	getdata(b_lines - 1, 0, "您想刪除其他重複登入的連線嗎？[Y/n] ",
		genbuf, 3, LCECHO);

	usleep(random()%5000000); // 0~5s
	if (genbuf[0] != 'n') {
	    do {
		// scan again, old ui may be invalid
		ui = getotherlogin(1);
		if(ui==NULL)
		    return;
		if (ui->pid > 0) {
		    if(kill(ui->pid, SIGHUP)<0) {
			perror("kill SIGHUP fail");
			break;
		    }
		    log_usies("KICK ", cuser.nickname);
		}  else {
		    fprintf(stderr, "id=%s ui->pid=0\n", cuser.userid);
		}
		usleep(random()%2000000+1000000); // 1~3s
	    } while(getotherlogin(3) != NULL);
	} else {
	    /* deny login if still have 3 */
	    if (getotherlogin(3) != NULL) {
		sleep(1);
		abort_bbs(0);	/* Goodbye(); */
	    }
	}
    } else {
	/* allow multiple guest user */
	if (search_ulistn(usernum, MAX_GUEST) != NULL) {
	    sleep(1);
	    vmsg("抱歉，目前已有太多 guest 在站上, 請用new註冊。");
	    exit(1);
	}
    }
}

static int
load_current_user(const char *uid)
{
    // userid should be already normalized.
    int is_admin_only = 0;

#ifdef ADMIN_PORT
    // ----------------------------------------------------- PORT TESTING
    // XXX currently this does not work if we're using tunnel.
    is_admin_only = (listen_port == ADMIN_PORT);
#endif

    // ----------------------------------------------------- NEW ACCOUNT

#ifdef STR_REGNEW
    if (!is_admin_only && strcasecmp(uid, STR_REGNEW) == 0) {

# ifndef LOGINASNEW
	assert(false);
	exit(0);
# endif // !LOGINASNEW

	new_register();
	clear();
	mkuserdir(cuser.userid);
	reginit_fav();
    } else
#endif

    // ----------------------------------------------------- RECOVER ACCOUNT

#ifdef STR_RECOVER
    if (!is_admin_only && strcasecmp(uid, STR_RECOVER) == 0) {
	// Allow more time due to sending emails for verification code.
	alarm(1800);
	recover_account();
	// Should have exited.
	assert(false);
	exit(1);
    } else
#endif

    // --------------------------------------------------- GUEST ACCOUNT

#ifdef STR_GUEST
    if (!is_admin_only && strcasecmp(uid, STR_GUEST) == 0) {
	if (initcuser(STR_GUEST)< 1) exit (0) ;
	pwcuInitGuestPerm();
	// can we prevent mkuserdir() here?
	mkuserdir(cuser.userid);
    } else
#endif

    // ---------------------------------------------------- USER ACCOUNT
    {
	if (!cuser.userid[0] && initcuser(uid) < 1)
            exit(0);

        if (is_admin_only) {
            if (!HasUserPerm(PERM_SYSOP | PERM_BBSADM | PERM_BOARD |
                             PERM_ACCOUNTS | PERM_CHATROOM |
                             PERM_VIEWSYSOP | PERM_PRG)) {
                puts("\r\n權限不足，請換其它 port 連線。\r\n");
                exit(0);
            }
        }

#ifdef LOCAL_LOGIN_MOD
	LOCAL_LOGIN_MOD();
#endif
	if (strcasecmp(str_sysop, cuser.userid) == 0){
#ifdef NO_SYSOP_ACCOUNT
	    exit(0);
#else /* 自動加上各個主要權限 */
	    // TODO only allow in local connection?
	    pwcuInitAdminPerm();
#endif
	}
	/* 早該有 home 了, 不知道為何有的帳號會沒有, 被砍掉了? */
	mkuserdir(cuser.userid);
	logattempt(cuser.userid, ' ', login_start_time, fromhost);
	ensure_user_agreement_version();
    }

    // check multi user
    multi_user_check();
    return 1;
}
