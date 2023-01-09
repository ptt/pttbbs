#pragma once

#include "bbs.h"
#include "mbbsd_s.h"

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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
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
#pragma GCC diagnostic pop

static void
check_BM(void)
{
  int i, total;

  assert(HasUserPerm(PERM_BM));
  for( i = 0, total = num_boards() ; i < total ; ++i )
    if( is_BM_cache(i + 1) ) /* XXXbid */
      return;

  // disable BM permission
  pwcuBitDisableLevel(PERM_BM);
  clear();
  outs("\n由於您已有一段時間未再擔任任何看板板主，\n"
       "\n板主權(包含進入BM板及加大信箱等)已被收回。\n");
  pressanykey();
}

static void
setup_utmp(int mode)
{
  /* NOTE, 在 getnewutmpent 之前不應該有任何 slow/blocking function */
  userinfo_t      uinfo = {0};
  uinfo.pid	    = currpid = getpid();
  uinfo.uid	    = usernum;
  uinfo.mode	    = currstat = mode;

  uinfo.userlevel = cuser.userlevel;
  uinfo.lastact   = time(NULL);

  fprintf(stderr, "mbbsd_s.setup_utmp: to inet_addr\n");
  // only enable this after you've really changed talk.c to NOT use from_alias.
  uinfo.from_ip   = inet_addr(fromhost);
  fprintf(stderr, "mbbsd_s.setup_utmp: after inet_addr: from_ip: %u\n", uinfo.from_ip);

  strlcpy(uinfo.userid,   cuser.userid,   sizeof(uinfo.userid));
  strlcpy(uinfo.nickname, cuser.nickname, sizeof(uinfo.nickname));
  strlcpy(uinfo.from,	    fromhost,	    sizeof(uinfo.from));

  uinfo.five_win  = cuser.five_win;
  uinfo.five_lose = cuser.five_lose;
  uinfo.five_tie  = cuser.five_tie;
  uinfo.chc_win   = cuser.chc_win;
  uinfo.chc_lose  = cuser.chc_lose;
  uinfo.chc_tie   = cuser.chc_tie;
  uinfo.chess_elo_rating = cuser.chess_elo_rating;
  uinfo.go_win    = cuser.go_win;
  uinfo.go_lose   = cuser.go_lose;
  uinfo.go_tie    = cuser.go_tie;
  uinfo.dark_win    = cuser.dark_win;
  uinfo.dark_lose   = cuser.dark_lose;
  uinfo.dark_tie    = cuser.dark_tie;
  uinfo.invisible = (cuser.invisible % 2) && (!HasUserPerm(PERM_VIOLATELAW));
  uinfo.pager	    = cuser.pager % PAGER_MODES;
  uinfo.withme    = cuser.withme & ~WITHME_ALLFLAG;

  if(cuser.withme & (cuser.withme<<1) & (WITHME_ALLFLAG<<1))
    uinfo.withme = 0;

  getnewutmpent(&uinfo);

  //////////////////////////////////////////////////////////////////
  // 以下可以進行比較花時間的運算。
  //////////////////////////////////////////////////////////////////

  currmode = MODE_STARTED;
  SHM->UTMPneedsort = 1;

  strip_nonebig5((unsigned char *)currutmp->nickname, sizeof(currutmp->nickname));

#ifdef FROMD
  // resolve fromhost
  {
    int fd;
    if ((fd = toconnect(FROMD_ADDR)) >= 0) {
      char *p, *ptr;
      char buf[STRLEN] = {0};

      write(fd, fromhost, strlen(fromhost));
      shutdown(fd, SHUT_WR);

      read(fd, buf, sizeof(buf) - 1); // keep trailing zero
      close(fd);

      // Check for newline separating source name and country
      p = strtok_r(buf, "\n", &ptr);

      // copy to currutmp
      if (p) {
        strlcpy(currutmp->from, p, sizeof(currutmp->from));

        // Did fromd send country name separately?
        p = strtok_r(NULL, "\n", &ptr);
        if (p)
          strlcpy(from_cc, p, sizeof(from_cc));
      }
    }
  }
#endif // WHERE

  /* Very, very slow friend_load. */
  if( strcmp(cuser.userid, STR_GUEST) != 0 ) // guest 不處理好友
    friend_load(0, 1);

  nice(3);
}

inline static void welcome_msg(void)
{
  prints(ANSI_RESET "      歡迎您再度拜訪，上次您是從 "
         ANSI_COLOR(1;33) "%s" ANSI_COLOR(0;37) " 連往本站。"
         ANSI_CLRTOEND "\n", cuser.lasthost);
  pressanykey();
}

inline static void check_bad_login(void)
{
  char            genbuf[200];
  setuserfile(genbuf, FN_BADLOGIN);
  if (more(genbuf, NA) != -1) {
    move(b_lines - 3, 0);
    outs("通常並沒有辦法知道該ip是誰所有, "
         "以及其意圖(是不小心按錯或有意測您密碼)\n"
         "若您有帳號被盜用疑慮, 請經常更改您的密碼或使用加密連線");
    if (vans("您要刪除以上錯誤嘗試的記錄嗎? [Y/n] ") != 'n')
      unlink(genbuf);
  }
}

inline static void append_log_recent_login()
{
  char buf[STRLEN], logfn[PATHLEN];
  int szlogfn = 0, szlogentry = 0;

  // prepare log format
  snprintf(buf, sizeof(buf), "%s %-15s\n",
           Cdatelite(&login_start_time), fromhost);
  szlogentry = strlen(buf);	// should be the same for all entries

  setuserfile(logfn, FN_RECENTLOGIN);
  szlogfn = dashs(logfn);
  if (szlogfn > SZ_RECENTLOGIN) {
    // rotate to 1/4 of SZ_RECENTLOGIN
    delete_records(logfn, szlogentry, 1,
                   (szlogfn-(SZ_RECENTLOGIN/4)) / szlogentry);
  }
  log_file(logfn, LOG_CREAT, buf);
}

inline static void check_mailbox_quota(void)
{
  if (!HasUserPerm(PERM_READMAIL))
    return;

  if (chkmailbox()) do {
      m_read();
    } while (chkmailbox_hard_limit());
}

static void init_guest_info(void)
{
  pwcuInitGuestInfo();
  currutmp->pager = PAGER_DISABLE;
}

#if FOREIGN_REG_DAY > 0
inline static void foreign_warning(void){
  if ((HasUserFlag(UF_FOREIGN)) && !(HasUserFlag(UF_LIVERIGHT))){
    if (login_start_time - cuser.firstlogin > (FOREIGN_REG_DAY - 5) * 24 * 3600){
      mail_muser(cuser, "[出入境管理局]", "etc/foreign_expired_warn");
    }
    else if (login_start_time - cuser.firstlogin > FOREIGN_REG_DAY * 24 * 3600){
      pwcuBitDisableLevel(PERM_LOGINOK | PERM_POST);
      vmsg("警告：請至出入境管理局申請永久居留");
    }
  }
}
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
static void
user_login(void)
{
#pragma clang diagnostic pop
  struct tm       ptime, lasttime;
  int             nowusers, i;

  /* NOTE! 在 setup_utmp 之前, 不應該有任何 blocking/slow function,
   * 否則可藉機 race condition 達到 multi-login */

  /* 初始化 uinfo、flag、mode */
  fprintf(stderr, "mbbsd_s.user_login: start\n");
  setup_utmp(LOGIN);

  fprintf(stderr, "mbbsd_s.user_login: after setup_utmp: LOGIN: %d\n",  LOGIN);

  /* log usies */
  STATINC(STAT_MBBSD_ENTER);
  log_usies("ENTER", fromhost);

  fprintf(stderr, "mbbsd_s.user_login: to setproctitle\n");
#ifndef VALGRIND
  setproctitle("%s: %s", margs, cuser.userid);
#endif

  fprintf(stderr, "mbbsd_s.user_login: to get local time\n");

  /* get local time */
  localtime4_r(&now, &ptime);
  localtime4_r(&cuser.lastlogin, &lasttime);

  fprintf(stderr, "mbbsd_s.user_login: to redrawwin\n");

  redrawwin();

  /* mask fromhost a.b.c.d to a.b.c.* */
  strlcpy(fromhost_masked, fromhost, sizeof(fromhost_masked));
  obfuscate_ipstr(fromhost_masked);

#ifndef MULTI_WELCOME_LOGIN
  fprintf(stderr, "[mbbsd_s.user_login] to more etc/Welcome_login\n");
  more("etc/Welcome_login", NA);
#else
  if( SHM->GV2.e.nWelcomes ){
    fprintf(stderr, "[mbbsd_s.user_login] GV2.e.nWelcomes\n");
    char            buf[80];
    snprintf(buf, sizeof(buf), "etc/Welcome_login.%d",
             (int)login_start_time % SHM->GV2.e.nWelcomes);
    more(buf, NA);
  }
#endif

  fprintf(stderr, "[mbbsd_s.user_login] to refresh\n");
  refresh();
  currutmp->alerts |= load_mailalert(cuser.userid);

  if ((nowusers = SHM->UTMPnumber) > SHM->max_user) {
    SHM->max_user = nowusers;
    SHM->max_time = now;
  }

  fprintf(stderr, "[mbbsd_s.user_login] to do_aloha\n");
  if (!(HasUserPerm(PERM_SYSOP) && HasUserPerm(PERM_SYSOPHIDE)) &&
      !HasUserRole(ROLE_HIDE_FROM) && !currutmp->invisible) {
    /* do_aloha is costly. do it later? And don't alert if previous
     * login was just minutes ago... */
    do_aloha("<<上站通知>> -- 我來啦！");
  }

  fprintf(stderr, "[mbbsd_s.user_login] to getmessage\n");
  if (SHM->loginmsg.pid){
    if(search_ulist_pid(SHM->loginmsg.pid))
      getmessage(SHM->loginmsg);
    else
      SHM->loginmsg.pid=0;
  }

  if (cuser.userlevel) {	/* not guest */
    move(t_lines - 4, 0);
    clrtobot();
    fprintf(stderr, "[mbbsd_s.user_login: to welcome_msg\n");
    welcome_msg();
    fprintf(stderr, "[mbbsd_s.user_login: after welcome_msg\n");

    append_log_recent_login();
    check_bad_login();
#ifdef USE_CHECK_BAD_CLIENTS
    check_bad_clients();
#endif
    check_register();
    pwcuLoginSave();	// is_first_login_of_today is only valid after pwcuLoginSave.
    // cuser.lastlogin 由 pwcuLoginSave 後值就變了，要看 last_login_time
    restore_backup();
    check_mailbox_quota();

    if (!HasUserPerm(PERM_BASIC)) {
      vs_hdr2(" 停權通知 ", " 部份功\能已被暫停使用");
      outs(ANSI_COLOR(1;31) "\n\n\t抱歉，你的帳號已被停權。\n"
           "\t詳情請至 ViolateLaw 看板搜尋你的 ID。\n" ANSI_RESET);
      pressanykey();
    }

    // XXX 這個 check 花不少時間，有點間隔比較好
    if (HasUserPerm(PERM_BM) &&
        (cuser.numlogindays % 10 == 0) &&	// when using numlogindays, check with is_first_login_of_today
        is_first_login_of_today )
      check_BM();		/* 自動取下離職板主權力 */

    // XXX only for temporary...
#ifdef ADBANNER_USONG_TIMEBOMB
    if (last_login_time < ADBANNER_USONG_TIMEBOMB)
    {
      if (query_adbanner_usong_pref_changed(cuser_ref, 1))
        pwcuToggleUserFlag(UF_ADBANNER_USONG);
    }
#endif
#ifdef UNTRUSTED_FORWARD_TIMEBOMB
    {   char fwd_path[PATHLEN];
      setuserfile(fwd_path, FN_FORWARD);
      if (dashf(fwd_path) && dasht(fwd_path) < UNTRUSTED_FORWARD_TIMEBOMB)
      {
        vs_hdr("自動轉寄設定已變更");
        unlink(fwd_path);
        outs("\n由於系統調整，您的自動轉寄已被重設，\n"
             "如有需求請重新設定。\n");
        pressanykey();
      }
    }
#endif

  } else if (strcmp(cuser.userid, STR_GUEST) == 0) { /* guest */

    init_guest_info();
    pressanykey();

  } else {
    // XXX no userlevel, no guest - what is this?
    clear();
    outs("抱歉，您的帳號資料異常或已被停權。\n");
    pressanykey();
    exit(1);
  }

  if(ptime.tm_yday!=lasttime.tm_yday)
    STATINC(STAT_TODAYLOGIN_MAX);

  if (!PERM_HIDE(currutmp)) {
    /* If you wanna do incremental upgrade
     * (like, added a function/flag that wants user to confirm againe)
     * put it here.
     * But you must use 'lasttime' because cuser.lastlogin
     * is already changed.
     */

    /* login time update */
    if(ptime.tm_yday!=lasttime.tm_yday)
      STATINC(STAT_TODAYLOGIN_MIN);
  }

#if FOREIGN_REG_DAY > 0
  foreign_warning();
#endif

  if(HasUserFlag(UF_FAV_ADDNEW)) {
    fav_load();
    if (get_fav_root() != NULL) {
      int num;
      num = updatenewfav(1);
      if (num > NEW_FAV_THRESHOLD &&
          vansf("找到 %d 個新看板，確定要加入我的最愛嗎？[y/N]", num) != 'y') {
        fav_free();
        fav_load();
      }
    }
  }

  for (i = 0; i < NUMVIEWFILE; i++)
  {
    if ((cuser.loginview >> i) & 1)
    {
      const char *fn = loginview_file[(int)i][0];
      // fn == '\0': ignore; NULL: break;
      if (!fn)
        break;

      switch (*fn) {
        case '\0':
          // simply ignore it.
          break;

        default:
          // use NA+pause or YEA?
          more(fn, YEA);
          break;
      }
    }
  }
}
