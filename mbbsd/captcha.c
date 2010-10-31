/* $Id$ */
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
    snprintf(cmd, sizeof(cmd), FIGLET_PATH " %s %s > %s",
	    opts, buf, fpath);

    // vmsg(cmd);
    if (system(cmd) != 0)
	return 0;

    return 1;
}


static int
_vgetcb_data_upper(int key, VGET_RUNTIME *prt, void *instance)
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
_vgetcb_data_change(int key, VGET_RUNTIME *prt, void *instance)
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
	    if (!gen_captcha(captcha, sizeof(captcha), fpath) ||
		    !dashf(fpath))
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

