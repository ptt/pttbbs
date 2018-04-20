#include "bbs.h"

////////////////////////////////////////////////////////////////////////////
// Figlet Captcha System
////////////////////////////////////////////////////////////////////////////
#ifdef USE_FIGLET_CAPTCHA

#define FN_JOBSPOOL_DIR	"jobspool/"

static int
gen_captcha(char *buf, int szbuf, char *fpath)
{
    // do not use: GQV
    const char *alphabet = "ABCDEFHIJKLMNOPRSTUWXYZ";
    int calphas = strlen(alphabet);
    char cmd[PATHLEN], opts[PATHLEN];
    int i, coptSpace, coptFont;
    static const char *optSpace[] = {
	"-S", "-s", "-k", "-W",
	// "-o",
	NULL
    };
    static const char *optFont[] = {
	"banner", "big", "slant", "small",
	"smslant", "standard",
	// block family
	"block", "lean",
	// shadow family
	// "shadow", "smshadow",
	// mini (better with large spacing)
	// "mini",
	NULL
    };

    // fill captcha code
    for (i = 0; i < szbuf-1; i++)
	buf[i] = alphabet[random() % calphas];
    buf[i] = 0; // szbuf-1

    // decide options
    coptSpace = coptFont = 0;
    while (optSpace[coptSpace]) coptSpace++;
    while (optFont[coptFont]) coptFont++;
    snprintf(opts, sizeof(opts),
	    "%s -f %s",
	    optSpace[random() % coptSpace],
	    optFont [random() % coptFont ]);

    // create file
    snprintf(fpath, PATHLEN, FN_JOBSPOOL_DIR ".captcha.%s", buf);
    snprintf(cmd, sizeof(cmd), FIGLET_PATH
             " %s %s >%s 2>/dev/null || rm %s",
             opts, buf, fpath, fpath);

    // for mbbsd daemons, system() may return -1 with errno=ECHILD,
    // so we can only trust the command itself.
    // vmsg(cmd);
    system(cmd);
    return dashf(fpath) && dashs(fpath) > 1;
}


static int
_vgetcb_data_upper(int key, VGET_RUNTIME *prt GCC_UNUSED, void *instance GCC_UNUSED)
{
    if (key >= 'a' && key <= 'z')
	key = toupper(key);
    if (key < 'A' || key > 'Z')
    {
	bell();
	return VGETCB_NEXT;
    }
    return VGETCB_NONE;
}

static int
_vgetcb_data_change(int key GCC_UNUSED, VGET_RUNTIME *prt GCC_UNUSED, void *instance GCC_UNUSED)
{
    char *s = prt->buf;
    while (*s)
    {
	if (isascii(*s) && islower(*s))
	    *s = toupper(*s);
	s++;
    }
    return VGETCB_NONE;
}

int verify_captcha(const char *reason)
{
    char captcha[7] = "", code[STRLEN];
    char fpath[PATHLEN];
    VGET_CALLBACKS vge = { NULL, _vgetcb_data_upper, _vgetcb_data_change };
    int tries = 0, i;

    do {
	// create new captcha
	if (tries % 2 == 0 || !captcha[0])
	{
	    // if generation failed, skip captcha.
	    if (!gen_captcha(captcha, sizeof(captcha), fpath))
		return 1;

	    // prompt user about captcha
	    vs_hdr("CAPTCHA 驗證程序");
	    outs(reason);
	    outs("請輸入下面圖樣顯示的文字。\n"
		    "圖樣只會由大寫的 A-Z 英文字母組成。\n\n");
	    show_file(fpath, 4, b_lines-5, SHOWFILE_ALLOW_ALL);
	    unlink(fpath);
	}

	// each run waits 10 seconds.
	for (i = 10; i > 0; i--)
	{
	    move(b_lines-1, 0); clrtobot();
	    prints("請仔細檢查上面的圖形， %d 秒後即可輸入...", i);
	    // flush out current input
	    doupdate();
	    sleep(1);
	    vkey_purge();
	}

	// input captcha
	move(b_lines-1, 0); clrtobot();
	prints("請輸入圖樣顯示的 %d 個英文字母: ", (int)strlen(captcha));
	vgetstring(code, strlen(captcha)+1, 0, "", &vge, NULL);

	if (code[0] && strcasecmp(code, captcha) == 0)
	    break;

	// error case.
	if (++tries >= 10)
	    return 0;

	// error
	vmsg("輸入錯誤，請重試。注意組成文字全是大寫英文字母。");

    } while (1);

    clear();
    return 1;
}
#else // !USE_FIGLET_CAPTCHA

int
verify_captcha(const char *reason)
{
    return 1;
}

#endif // !USE_FIGLET_CAPTCHA

////////////////////////////////////////////////////////////////////////////
// Remote Captcha System
////////////////////////////////////////////////////////////////////////////
#ifdef USE_REMOTE_CAPTCHA

static void captcha_random(char *buf, size_t len)
{
    // prevent ambigious characters: oOlI
    const char * const chars = "qwertyuipasdfghjkzxcvbnmoQWERTYUPASDFGHJKLZXCVBNM";

    for (int i = 0; i < len; i++)
	buf[i] = chars[random() % strlen(chars)];
    buf[len] = '\0';
}

static const char *
captcha_insert_remote(const char *handle, const char *verify)
{
    int ret, code = 0;
    char uri[320];
    snprintf(uri, sizeof(uri), "%s?secret=%s&handle=%s&verify=%s",
	     CAPTCHA_INSERT_URI, CAPTCHA_INSERT_SECRET, handle, verify);
    THTTP t;
    thttp_init(&t);
    ret = thttp_get(&t, CAPTCHA_INSERT_SERVER_ADDR, uri, CAPTCHA_INSERT_HOST);
    if (!ret)
	code = thttp_code(&t);
    thttp_cleanup(&t);

    if (ret)
	return "伺服器連線失敗, 請稍後再試.";
    if (code != 200)
	return "內部伺服器錯誤, 請稍後再試.";
    return NULL;
}

const char *
remote_captcha()
{
    const char *msg = NULL;
    char handle[CAPTCHA_CODE_LENGTH + 1];
    char verify[CAPTCHA_CODE_LENGTH + 1];
    char verify_input[CAPTCHA_CODE_LENGTH + 1];

    vs_hdr("取得驗證碼");

    captcha_random(handle, CAPTCHA_CODE_LENGTH);
    captcha_random(verify, CAPTCHA_CODE_LENGTH);

    msg = captcha_insert_remote(handle, verify);
    if (msg)
	return msg;

    move(2, 0);
    outs("請先至以下連結取得認證碼:\n");
    outs(CAPTCHA_URL_PREFIX "?handle=");
    outs(handle);
    outs("\n");

    for (int i = 3; i > 0; i--) {
	if (i < 3) {
	    char buf[80];
	    snprintf(buf, sizeof(buf), ANSI_COLOR(1;31)
		     "驗證碼錯誤, 您還有 %d 次機會." ANSI_RESET, i);
	    move(6, 0);
	    outs(buf);
	}
	verify_input[0] = '\0';
	getdata(5, 0, "請輸入驗證碼: ", verify_input,
		sizeof(verify_input), DOECHO);
	if (!strcmp(verify, verify_input))
	    return NULL;
    }
    return "驗證碼輸入錯誤次數太多, 請重新操作!";
}

#else

const char *
remote_captcha()
{
    return NULL;
}

#endif // !USE_REMOTE_CAPTCHA
