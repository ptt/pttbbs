/* $Id$ */
#include "bbs.h"

// Panty & Stocking Browser
//
// A generic framework for displaying pre-generated data by a simplified
// page-view user interface.
//
// Author: Hung-Te Lin (piaip)
// --------------------------------------------------------------------------
// Copyright (c) 2010 Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved.
// Distributed under BSD license (GPL compatible).
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
// --------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// Constant
#define PSB_EOF (-1)
#define PSB_NA  (-2)
#define PSB_NOP (-3)

///////////////////////////////////////////////////////////////////////////
// Data Structure
typedef struct {
    int curr, total, header_lines, footer_lines;
    int key;
    int allow_pbs_version_message;
    void *ctx;
    int (*header)(void *ctx); 
    int (*footer)(void *ctx); 
    int (*renderer)(int i, int curr, int total, int rows, void *ctx);
    int (*cursor)(int y, int curr, void *ctx);
    int (*input_processor)(int key, int curr, int total, int rows, void *ctx);
} PSB_CTX;

static int
psb_default_header(void *ctx) {
    vs_hdr2bar("Panty & Stocking Browser", BBSNAME);
    return 0;
}

static int
psb_default_footer(void *ctx) {
    vs_footer(" PSB 1.0 ",
              " (↑/↓/PgUp/PgDn/0-9)Move (Enter/→)Select \t(q/←)Quit");
    return 0;
}

static int
psb_default_renderer(int i, int curr, int total, int rows, void *ctx) {
    prints("   %s(Demo) %5d / %5d Item\n", (i == curr) ? "*" : " ", i, total);
    return 0;
}

static int
psb_default_cursor(int y, int curr, void * ctx) {
#ifdef USE_PFTERM
    outs("●\b");
#else
    cursor_show(y, 0);
#endif
    return 0;
}

static int
psb_default_input_processor(int key, int curr, int total, int rows, void *ctx) {
    switch(key) {
        case 'q':
        case KEY_LEFT:
            return PSB_EOF;

        case KEY_HOME:
        case '0':
            return 0;

        case KEY_END:  
        case '$':
            return total-1;

        case KEY_PGUP:
        case Ctrl('B'):
        case 'N':
            if (curr / rows > 0)
                return curr - rows;
            return 0;

        case KEY_PGDN:
        case Ctrl('F'):
        case 'P':
            if (curr + rows < total)
                return curr + rows;
            return total - 1;

        case KEY_UP: 
        case Ctrl('P'):
        case 'p':
        case 'k':
            return (curr > 0) ? curr-1 : curr;

        case KEY_DOWN:
        case Ctrl('N'):
        case 'n':
        case 'j':
            return (curr + 1 < total) ? curr + 1 : curr;

        default:
            if (key >= '0' && key <= '9') {
                int newval = search_num(key, total);
                if (newval >= 0 && newval < total)
                    return newval;
                return curr;
            }
            break;
    }
    return  PSB_NA;
}

static void
psb_init_defaults(PSB_CTX *psbctx) {
    // pre-setup
    assert(psbctx);
    if (!psbctx->header)
        psbctx->header = psb_default_header;
    if (!psbctx->footer)
        psbctx->footer = psb_default_footer;
    if (!psbctx->renderer)
        psbctx->renderer = psb_default_renderer;
    if (!psbctx->cursor)
        psbctx->cursor = psb_default_cursor;

    assert(psbctx->curr >= 0 &&
           psbctx->total >= 0 &&
           psbctx->curr < psbctx->total);
    assert(psbctx->header_lines > 0 &&
           psbctx->footer_lines);
}

int
psb_main(PSB_CTX *psbctx)
{
    psb_init_defaults(psbctx);

    while (1) {
        int i;
        int rows = t_lines - psbctx->header_lines - psbctx->footer_lines;
        int base;

        assert(rows > 0);
        base = psbctx->curr / rows * rows;
        clear();
        SOLVE_ANSI_CACHE();
        psbctx->header(psbctx->ctx);
        for (i = 0; i < rows; i++) {
            move(psbctx->header_lines + i, 0);
            SOLVE_ANSI_CACHE();
            if (base + i < psbctx->total)
                psbctx->renderer(base + i, psbctx->curr, psbctx->total, 
                                 rows, psbctx->ctx);
        }
        move(t_lines - psbctx->footer_lines, 0);
        SOLVE_ANSI_CACHE();
        psbctx->footer(psbctx->ctx);
        if (psbctx->allow_pbs_version_message) {
            SOLVE_ANSI_CACHE();
            prints(ANSI_COLOR(0;1;30) "%*s" ANSI_RESET, t_columns-2,
                   "-- Powered by Panty & Stocking Browser System");
        }
        i = psbctx->header_lines + psbctx->curr - base;
        move(i, 0);
        psbctx->cursor(i, psbctx->curr, psbctx->ctx);
        psbctx->key = vkey();

        i = PSB_NA;
        if (psbctx->input_processor)
            i = psbctx->input_processor(psbctx->key, psbctx->curr,
                                        psbctx->total, rows, psbctx->ctx);
        if (i == PSB_NA)
            i = psb_default_input_processor(psbctx->key, psbctx->curr,
                                            psbctx->total, rows, psbctx->ctx);
        if (i == PSB_EOF)
            break;
        if (i == PSB_NOP)
            continue;

        if (i >=0 && i < psbctx->total)
            psbctx->curr = i;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////
// Time Capsule: Edit History

#define PVEH_LIMIT_NUMBER   (199)

typedef struct {
    const char *subject;
    const char *filebase;
    int leave_for_recycle_bin;
    int rev_base;
    int base_as_current;
    time4_t *timestamps;
} pveh_ctx;

static int
pveh_header(void *ctx) {
    pveh_ctx *cx = (pveh_ctx*) ctx;
    vs_hdr2barf(" 【" TIME_CAPSULE_NAME ": 編輯歷史】 \t %s", cx->subject);
    move(1, 0);
    outs("請注意本系統不會永久保留所有的編輯歷史。");
    outs("\n");
    return 0;
}

static int
pveh_footer(void *ctx) {
    vs_footer(" 編輯歷史 ",
              " (↑/↓/PgUp/PgDn/0-9)移動 (Enter/r/→)選擇 (~)" RECYCLE_BIN_NAME
              "\t(q/←)跳出");
    move(b_lines-1, 0);
    return 0;
}

static void
pveh_solve_rev_filename(int rev, int i, char *fname, size_t sz_fname,
                        pveh_ctx *cx) {
    if (cx->base_as_current && i == 0)
        strlcpy(fname, cx->filebase, sz_fname);
    else
        timecapsule_get_by_revision(
                cx->filebase, rev + cx->rev_base, fname, sz_fname);
}

static int
pveh_renderer(int i, int curr, int total, int rows, void *ctx) {
    const char *subject = "";
    char fname[PATHLEN];
    time4_t ftime = 0;
    pveh_ctx *cx = (pveh_ctx*) ctx;
    int rev = total - i; // i/curr = 0 based, rev = 1 based

    if (cx->timestamps[i] == 0) {
        pveh_solve_rev_filename(rev, i, fname, sizeof(fname), cx);
        ftime = dasht(fname);
        if (!ftime)
            ftime++;
        cx->timestamps[i] = ftime;
    } else {
        ftime = cx->timestamps[i];
    }

    if (ftime != -1)
        subject = Cdate(&ftime);
    else
        subject = "(記錄已過保留期限/已清除)";

    prints("   %s%s  版本: ",
           (i == curr) ? ANSI_COLOR(1;41;37) : "",
           (ftime == -1) ? ANSI_COLOR(1;30) : "");
    if (cx->base_as_current && i == 0)
        outs("[目前版本]");
    else
        prints("#%09d", rev + cx->rev_base);
    prints("  時間: %-*s" ANSI_RESET "\n", t_columns - 31, subject);
    return 0;
}

static int
pveh_input_processor(int key, int curr, int total, int rows, void *ctx) {
    char fname[PATHLEN];
    pveh_ctx *cx = (pveh_ctx*) ctx;
    int rev = total - curr; // see renderer

    switch (key) {
        case KEY_ENTER:
        case KEY_RIGHT:
        case 'r':
            pveh_solve_rev_filename(rev, curr, fname, sizeof(fname), cx);
            more(fname, YEA);
            return PSB_NOP;

        case '~':
            cx->leave_for_recycle_bin = 1;
            return PSB_EOF;
    }
    return PSB_NA;
}

static int
pveh_welcome() {
    // warning screen!
    static char is_first_enter_pveh = 1;

    if (is_first_enter_pveh) {
        is_first_enter_pveh = 0;
        clear();
        move(2, 0);
        outs(ANSI_COLOR(1;31)
"  歡迎使用 Time Capsule 的編輯歷史瀏覽系統!\n\n" ANSI_RESET
"  提醒您: (1) 此系統尚在實驗性開放中，站方未來會決定是否繼續提供此功\能。\n\n"
"          (2) 所有的資料僅供參考，站方不保證此處為完整的電磁記錄。\n\n"
"          (3) 所有的資料都可能不定期由系統清除掉。\n"
"              無編輯歷史不能代表沒有編輯過，也可能是被清除了\n\n"
"   Mini FAQ:\n\n"
"   Q: 怎樣才會有歷史記錄 (增加版本數)?\n"
"   A: 在系統更新後每次使用 E 編輯文章並存檔就會有記錄。推文不會增加記錄版本數\n\n"
"   Q: 通常歷史會保留多久?\n"
"   A: 仍在評估中，或許\會是兩週到一個月\n\n"
"   Q: 檔案被刪了也可以看歷史嗎?\n"
"   A: 屍體還在看板上可以直接對<本文已被刪除>按，不然就先進" 
       RECYCLE_BIN_NAME "(~)再找\n"
            );
        doupdate();
        pressanykey();
    }
    return 0;
}


int
psb_view_edit_history(const char *base, const char *subject,
                      int maxrev, int current_as_base) {
    pveh_ctx pvehctx = {
        .subject = subject,
        .filebase = base,
        .rev_base = 0,
        .base_as_current = current_as_base,
    };
    PSB_CTX ctx = {
        .curr = 0,
        .total = maxrev + pvehctx.base_as_current,
        .header_lines = 3,
        .footer_lines = 2,
        .allow_pbs_version_message = 1,
        .ctx = (void*)&pvehctx,
        .header = pveh_header,
        .footer = pveh_footer,
        .renderer = pveh_renderer,
        .input_processor = pveh_input_processor,
    };

    pveh_welcome();

    if (maxrev > PVEH_LIMIT_NUMBER) {
        pvehctx.rev_base = maxrev - PVEH_LIMIT_NUMBER;
        ctx.total -= pvehctx.rev_base;
    }

    pvehctx.timestamps = (time4_t*) malloc (sizeof(time4_t) * ctx.total);
    if (!pvehctx.timestamps) {
        vmsgf("內部錯誤，請至" BN_BUGREPORT "看板報告，謝謝");
        return FULLUPDATE;
    }
    // load on demand!
    memset(pvehctx.timestamps, 0, sizeof(time4_t) * ctx.total);

    psb_main(&ctx);
    free(pvehctx.timestamps);
    return (pvehctx.leave_for_recycle_bin ?
            RET_RECYCLEBIN :
            FULLUPDATE);
}

///////////////////////////////////////////////////////////////////////////
// Time Capsule: Recycle Bin
#define PVRB_LIMIT_NUMBER   (10300)
// #define PVRB_LIMIT_NUMBER   (103000)

typedef struct {
    const char *dirbase;
    const char *subject;
    int modify_mailbox;
    int viewbase;
    fileheader_t *records;
} pvrb_ctx;

static int
pvrb_header(void *ctx) {
    pvrb_ctx *cx = (pvrb_ctx*) ctx;
    vs_hdr2barf(" 【" TIME_CAPSULE_NAME ": " RECYCLE_BIN_NAME "】 \t %s",
                cx->subject);
    move(1, 0);
    outs("請注意此處的檔案將不定期清除。\n");
    vbarf(ANSI_REVERSE "    編號 | 日 期 |   作  者   |   標      題\t");
    return 0;
}

static int
pvrb_footer(void *ctx) {
    vs_footer(" 已刪檔案 ",
              " (↑/↓/PgUp/PgDn/0-9)移動 (Enter/r/→)選擇 (x)存入信箱"
              "\t(q/←)跳出");
    move(b_lines-1, 0);
    return 0;
}

static int
pvrb_renderer(int i, int curr, int total, int rows, void *ctx) {
    pvrb_ctx *cx = (pvrb_ctx*) ctx;
    fileheader_t *fh = &cx->records[total - i - 1];

    // TODO make this load-on-demand
    // quick display, but lack of recommend counter...
    outs("   ");
    if (i == curr)
        // prints(ANSI_COLOR(1;40;3%d), i%8);
        outs(ANSI_COLOR(1;40;31));
    prints("%06d  %-5.5s  %-12.12s %s" ANSI_RESET "\n",
           total - i, fh->date, fh->owner, fh->title);
    return 0;
}

static int
pvrb_input_processor(int key, int curr, int total, int rows, void *ctx) {
    char fname[PATHLEN];
    int maxrev;
    pvrb_ctx *cx = (pvrb_ctx*) ctx;
    fileheader_t *fh = &cx->records[total - curr - 1];
    const char *err_no_rev = "抱歉，本文歷史資料已被系統清除。";

    switch (key) {
        // TODO add '/' for search
        case 'x':
            setdirpath(fname, cx->dirbase, fh->filename);
            maxrev = timecapsule_get_max_revision_number(fname);
            if (maxrev < 1) {
                vmsg(err_no_rev);
            } else {
                char revfname[PATHLEN];
                char ans[3];
                timecapsule_get_by_revision(
                        fname, maxrev, revfname, sizeof(revfname));
                getdata(b_lines-2, 0, "確定要把此份文件回存至信箱嗎? [y/N]: ",
                        ans, sizeof(ans), LCECHO);
                if (*ans == 'y') {
                    if (mail_log2id(cuser.userid, fh->title,
                                    revfname, RECYCLE_BIN_OWNER,
                                    1, 0) == 0) {
                        cx->modify_mailbox = 1;
                        vmsg("儲存完成，請至信箱檢查備忘錄信件");
                    } else
                        vmsg("儲存失敗，請至 " BN_BUGREPORT " 看板報告，謝謝");
                    return PSB_EOF;
                }
            }
            return PSB_NOP;

        case KEY_ENTER:
        case KEY_RIGHT:
        case 'r':
            setdirpath(fname, cx->dirbase, fh->filename);
            maxrev = timecapsule_get_max_revision_number(fname);
            if (maxrev == 1) {
                char revfname[PATHLEN];
                timecapsule_get_by_revision(
                        fname, 1, revfname, sizeof(revfname));
                more(revfname, YEA);
            } else if (maxrev > 1) {
                psb_view_edit_history(fname, fh->title, maxrev, 0);
            } else {
                vmsg(err_no_rev);
            }
            return PSB_NOP;
    }
    return PSB_NA;
}

static int
pvrb_welcome() {
    // warning screen!
    static char is_first_enter_pvrb = 1;

    if (is_first_enter_pvrb) {
        is_first_enter_pvrb = 0;
        clear(); SOLVE_ANSI_CACHE();
        move(2, 0);
        outs(ANSI_COLOR(1;36)
"  歡迎使用 " TIME_CAPSULE_NAME " " RECYCLE_BIN_NAME "!\n\n" ANSI_RESET
"  提醒您: (1) 此系統尚在實驗性開放中，站方未來會決定是否繼續提供此功\能。\n\n"
"          (2) 所有的資料僅供參考，站方不保證此處為完整的電磁記錄。\n\n"
"          (3) 所有的資料都可能不定期由系統清除掉。\n"
"              ^D 刪除的內容目前不會保留。\n\n"
"   Mini FAQ:\n\n"
"   Q: 通常檔案會保留多久?\n"
"   A: 仍在評估中，或許\會是一個到兩個月，另外篇數也會有上限。\n\n"
"   Q: 哪些地方有回收筒可用?\n"
"   A: 目前開放個人信箱(所有用戶)跟看板/精華區文章(板主限定)。\n"
"      精華區子目錄暫不支援。\n\n"
            );
        doupdate();
        pressanykey();
    }
    return 0;
}

int
psb_recycle_bin(const char *base, const char *title) {
    int nrecords = 0, viewbase = 0;
    pvrb_ctx pvrbctx = {
        .dirbase = base,
        .subject = title,
    };
    PSB_CTX ctx = {
        .curr = 0,
        .total = 0, // maxrev + pvrbctx.base_as_current,
        .header_lines = 3,
        .footer_lines = 2,
        .allow_pbs_version_message = 1,
        .ctx = (void*)&pvrbctx,
        .header = pvrb_header,
        .footer = pvrb_footer,
        .renderer = pvrb_renderer,
        .input_processor = pvrb_input_processor,
    };

    nrecords = timecapsule_get_max_archive_number(base, sizeof(fileheader_t));
    if (!nrecords) {
        vmsg("目前" RECYCLE_BIN_NAME "內無任何內容。");
        return FULLUPDATE;
    }

    pvrb_welcome();

    // truncate on large size
    if (nrecords > PVRB_LIMIT_NUMBER) {
        viewbase = nrecords - PVRB_LIMIT_NUMBER;
        nrecords -= viewbase;
    }
    ctx.total = nrecords;

    pvrbctx.records = (fileheader_t*) malloc (sizeof(fileheader_t) * nrecords);
    if (!pvrbctx.records) {
        vmsgf("內部錯誤，請至" BN_BUGREPORT "看板報告，謝謝");
        return FULLUPDATE;
    }
    timecapsule_get_archive_blobs(base, viewbase, nrecords, pvrbctx.records,
                                  sizeof(fileheader_t));
    psb_main(&ctx);
    free(pvrbctx.records);
    return pvrbctx.modify_mailbox ? DIRCHANGED : FULLUPDATE;
}

///////////////////////////////////////////////////////////////////////////
// Admin Edit

// still 偷懶...
#define MAX_PAE_ENTRIES (256)

typedef struct {
    char *descs[MAX_PAE_ENTRIES];
    char *files[MAX_PAE_ENTRIES];
} pae_ctx;

static int
pae_header(void *ctx) {
    vs_hdr2bar(" 【系統檔案】 ", "  編輯系統檔案");
    outs("請選取要編輯的檔案後按 Enter 開始修改\n");
    vbarf(ANSI_REVERSE
         "%5s %-36s%-30s", "編號", "名  稱", "檔  名");
    return 0;
}

static int
pae_footer(void *ctx) {
    vs_footer(" 編輯系統檔案 ",
              " (↑↓/0-9)移動 (Enter/e/r/→)編輯 (DEL/d)刪除 \t(q/←)跳出");
    move(b_lines-1, 0);
    return 0;
}

static int
pae_renderer(int i, int curr, int total, int rows, void *ctx) {
    pae_ctx *cx = (pae_ctx*) ctx;
    prints("  %3d %s%s%-36.36s " ANSI_COLOR(1;37) "%-30.30s" ANSI_RESET "\n", 
            i+1,
            (i == curr) ? ANSI_COLOR(41) : "",
            dashf(cx->files[i]) ? ANSI_COLOR(1;36) : ANSI_COLOR(1;30),
            cx->descs[i], cx->files[i]);
    return 0;
}

static int
pae_input_processor(int key, int curr, int total, int rows, void *ctx) {
    int result;
    pae_ctx *cx = (pae_ctx*) ctx;

    switch(key) {
        case KEY_DEL:
        case 'd':
            if (vansf("確定要刪除 %s 嗎？ (y/N) ", cx->descs[curr]) == 'y')
                unlink(cx->files[curr]);
            vmsgf("系統檔案[%s]: %s", cx->files[curr],
                  !dashf(cx->files[curr]) ?  "刪除成功\ " : "未刪除");
            return PSB_NOP;

        case KEY_ENTER:
        case KEY_RIGHT:
        case 'r':
        case 'e':
        case 'E':
            result = veditfile(cx->files[curr]);
            // log file change
            if (result != EDIT_ABORTED)
            {
                log_filef("log/etc_edit.log",
                          LOG_CREAT,
                          "%s %s %s # %s\n",
                          Cdate(&now),
                          cuser.userid,
                          cx->files[curr],
                          cx->descs[curr]);
            }
            vmsgf("系統檔案[%s]: %s",
                  cx->files[curr],
                  (result == EDIT_ABORTED) ?  "未改變" : "更新完畢");
            return PSB_NOP;
    }
    return PSB_NA;
}

int
psb_admin_edit() {
    int i;
    char buf[PATHLEN*2];
    FILE *fp;
    pae_ctx paectx = { {0}, };
    PSB_CTX ctx = {
        .curr = 0,
        .total = 0,
        .header_lines = 4,
        .footer_lines = 2,
        .allow_pbs_version_message = 1,
        .ctx = (void*)&paectx,
        .header = pae_header,
        .footer = pae_footer,
        .renderer = pae_renderer,
        .input_processor = pae_input_processor,
    };

    fp = fopen(FN_CONF_EDITABLE, "rt");
    if (!fp) {
	// you can find a sample in sample/etc/editable
	vmsgf("未設定可編輯檔案列表[%s]，請洽系統站長。", FN_CONF_EDITABLE);
	return 0;
    }

    // load the editable file.
    // format: filename [ \t]* description
    while (ctx.total < MAX_PAE_ENTRIES &&
           fgets(buf, sizeof(buf), fp)) {
        char *k = buf, *v = buf;
        if (!*buf || strchr("#./ \t\n\r", *buf))
            continue;

        // change \t to ' '.
        while (*v) if (*v++ == '\t') *(v-1) = ' ';
        v = strchr(buf, ' ');
        if (v == NULL)
            continue;

        // see if someone is trying to crack
        k = strstr(buf, "..");
        if (k && k < v)
            continue;

	// reject anything outside etc/ folder.
        if (strncmp(buf, "etc/", strlen("etc/")) != 0)
            continue;

        // adjust spaces
        chomp(buf);
        k = buf; *v++ = 0;
        while (*v == ' ') v++;
        trim(k);
        trim(v);

        // add into context
        paectx.files[ctx.total] = strdup(k);
        paectx.descs[ctx.total] = strdup(v);
        ctx.total++;
    }

    psb_main(&ctx);

    for (i = 0; i < ctx.total; i++) {
        free(paectx.files[i]);
        free(paectx.descs[i]);
    }
    return 0;
}

