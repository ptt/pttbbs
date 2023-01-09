#include "bbs.h"
#include "testmock.h"

// When CONVERT is applied, we may need to write extra N bytes into buffer for
// one character input. Currently the number is 3 (UTF8).
#ifdef CONVERT
# define OBUFMINSPACE (3)
#else
# define OBUFMINSPACE (1)
#endif

#ifdef DEBUG
#define register
#define inline
// #define DBG_OUTRPT
#endif

VBUF vout = {}, *pvout = &vout;
VBUF vin = {}, *pvin = &vin;

unsigned char OFLUSH_BUF[OBUFSIZE*5] = {};
unsigned char *pOFLUSH_BUF = OFLUSH_BUF;

// we've seen such pattern - make it accessible for movie mode.
#define CLIENT_ANTI_IDLE_STR   ESC_STR "OA" ESC_STR "OB"

#ifdef DBG_OUTRPT
// output counter
static unsigned long szTotalOutput = 0, szLastOutput = 0;
unsigned char fakeEscape = 0;

static unsigned char fakeEscFilter(unsigned char c)
{
  if (!fakeEscape) return c;
  if (c == ESC_CHR) return '*';
  else if (c == '\n') return 'N';
  else if (c == '\r') return 'R';
  else if (c == '\b') return 'B';
  else if (c == '\t') return 'I';
  return c;
}
#endif // DBG_OUTRPT

/* ----------------------------------------------------- */
/* debug reporting                                       */
/* ----------------------------------------------------- */

#if defined(DEBUG) || defined(DBG_OUTRPT)
void
debug_print_input_buffer(char *s, size_t len)
{
  int y, x, i;
  if (!s || !len)
    return;

  getyx_ansi(&y, &x);
  move(b_lines, 0); clrtoeol();
  SOLVE_ANSI_CACHE();
  prints("Input Buffer (%d): [ ", (int)len);
  for (i = 0; i < len; i++, s++)
  {
    int c = (unsigned char)*s;
    if (!isascii(c) || !isprint(c) || c == ' ')
    {
      if (c == ESC_CHR)
        outs(ANSI_COLOR(1;36) "Esc" ANSI_RESET);
      else if (c == ' ')
        outs(ANSI_COLOR(1;36) "Sp " ANSI_RESET);
      else if (c == 0)
        prints(ANSI_COLOR(1;31) "Nul" ANSI_RESET);
      else if (c > 0 && c < ' ')
        prints(ANSI_COLOR(1;32) "^%c", c + 'A' -1);
      else
        prints(ANSI_COLOR(1;33) "[%02X]" ANSI_RESET, c);
    } else {
      outc(c);
    }
  }
  prints(" ] ");
  move_ansi(y, x);
}
#endif

/* ----------------------------------------------------- */
/* Input Output System                                   */
/* ----------------------------------------------------- */
int
init_io() {
  vbuf_new(pvout, OBUFSIZE);
  vbuf_new(pvin, IBUFSIZE);
  return 0;
}

/* ----------------------------------------------------- */
/* output routines                                       */
/* ----------------------------------------------------- */

void reset_oflush_buf() {
  bzero(OFLUSH_BUF, sizeof(OFLUSH_BUF));
  pOFLUSH_BUF = OFLUSH_BUF;
}

void log_oflush_buf() {
  size_t len_buf = pOFLUSH_BUF - OFLUSH_BUF;
  fprintf(stderr, "[mock_io.log_oflush_buf] buf-len: %lu\n", len_buf);
  if(len_buf == 0) {
    return;
  }

  //fprintf(stderr, "|%s|\n", OFLUSH_BUF);

  fprintf(stderr, "(raw)\n");
  for(unsigned long i = 0; i < len_buf; i++) {
    if(i && i % 10 == 0) {
      fprintf(stderr, "\n");
    }
    fprintf(stderr, " %02X", OFLUSH_BUF[i]);
  }
  fprintf(stderr, "\n");
}

void
oflush(void)
{
  unsigned char *OFLUSH_BUF_END = OFLUSH_BUF + sizeof(OFLUSH_BUF);
  size_t lenbuf = OFLUSH_BUF_END - pOFLUSH_BUF;
  size_t lenpv = vbuf_size(pvout);

  fprintf(stderr, "[mock_io.oflush] start: len_pvout: %zu\n", lenpv);

  assert(pOFLUSH_BUF < OFLUSH_BUF_END);

  if(lenbuf < lenpv) {
    fprintf(stderr, "[mock_io.oflush] OFLUSH_BUF < pvout! lenbuf: %lu len_pvout: %lu\n", lenbuf, lenpv);
    return;
  }

  if (!vbuf_is_empty(pvout)) {
    STATINC(STAT_SYSWRITESOCKET);
    if(pvout->tail > pvout->head) {
      memcpy(pOFLUSH_BUF, pvout->head, lenpv);
      pOFLUSH_BUF += pvout->tail - pvout->head;
    } else if(pvout->tail < pvout->head) {
      size_t len_end_head = pvout->buf_end - pvout->head;
      memcpy(pOFLUSH_BUF, pvout->head, len_end_head);
      pOFLUSH_BUF += len_end_head;
      size_t len_tail_buf = pvout->tail - pvout->buf;
      memcpy(pOFLUSH_BUF, pvout->buf, len_tail_buf);
      pOFLUSH_BUF += len_tail_buf;
    }

    pvout->head = pvout->tail;
    // vbuf_write(pvout, 1, VBUF_RWSZ_ALL);
  }

#ifdef DBG_OUTRPT
  {
    static char xbuf[128];
    sprintf(xbuf, ESC_STR "[s" ESC_STR "[H" " [%lu/%lu] " ESC_STR "[u",
            szLastOutput, szTotalOutput);
    write(1, xbuf, strlen(xbuf));
    szLastOutput = 0;
  }
#endif // DBG_OUTRPT

  // XXX to flush, set TCP_NODELAY instead.
  // fsync does NOT work on network sockets.
  // fsync(1);
}

inline void
output(const char *s, int len)
{
  while (len-- > 0)
    ochar(*s++);
}

int
ochar(int c)
{
#ifdef DBG_OUTRPT
  // TODO we can support converted output in future.
  c = fakeEscFilter(c);
  szTotalOutput ++;
  szLastOutput ++;
#endif // DBG_OUTRPT

  if (vbuf_space(pvout) < OBUFMINSPACE)
    oflush();

#ifdef CONVERT
  convert_write(pvout, c);
#else
  vbuf_add(pvout, c);
#endif

  return 0;
}

/* ----------------------------------------------------- */
/* pager processor                                       */
/* ----------------------------------------------------- */

int
process_pager_keys(int ch)
{
  static int water_which_flag = 0;
  assert(currutmp);
  switch (ch)
  {
    case Ctrl('U') :
      if (!is_login_ready || !currutmp ||
          !HasUserPerm(PERM_BASIC) || HasUserPerm(PERM_VIOLATELAW))
        return ch;
      if ( currutmp->mode == EDITING ||
           currutmp->mode == LUSERS  ||
           !currutmp->mode) {
        return ch;
      } else {
        screen_backup_t old_screen;
        int             my_newfd;

        scr_dump(&old_screen);
        my_newfd = vkey_detach();

        t_users();

        vkey_attach(my_newfd);
        scr_restore(&old_screen);
      }
      return KEY_INCOMPLETE;

      // TODO 醜死了的 code ，等好心人 refine
    case Ctrl('R'):

      if (PAGER_UI_IS(PAGER_UI_OFO))
      {
        int my_newfd;
        screen_backup_t old_screen;

        if (!currutmp->msgs[0].pid ||
            wmofo != NOTREPLYING)
          break;

        scr_dump(&old_screen);

        my_newfd = vkey_detach();
        ofo_my_write();
        scr_restore(&old_screen);
        vkey_attach(my_newfd);
        return KEY_INCOMPLETE;

      }

      // non-UFO
      check_water_init();

      if (watermode > 0)
      {
        watermode = (watermode + water_which->count)
            % water_which->count + 1;
        t_display_new();
        return KEY_INCOMPLETE;
      }
      else if (watermode == 0 &&
               currutmp->mode == 0 &&
               (currutmp->chatid[0] == 2 || currutmp->chatid[0] == 3) &&
               water_which->count != 0)
      {
        /* 第二次按 Ctrl-R */
        watermode = 1;
        t_display_new();
        return KEY_INCOMPLETE;
      }
      else if (watermode == -1 &&
               currutmp->msgs[0].pid)
      {
        /* 第一次按 Ctrl-R (必須先被丟過水球) */
        screen_backup_t old_screen;
        int             my_newfd;
        scr_dump(&old_screen);

        /* 如果正在talk的話先不處理對方送過來的封包 (不去select) */
        my_newfd = vkey_detach();
        show_call_in(0, 0);
        watermode = 0;
#ifndef PLAY_ANGEL
        my_write(currutmp->msgs[0].pid, "水球丟過去： ",
                 currutmp->msgs[0].userid, WATERBALL_GENERAL, NULL);
#else
        switch (currutmp->msgs[0].msgmode) {
          case MSGMODE_TALK:
          case MSGMODE_WRITE:
          case MSGMODE_ALOHA:
            my_write(currutmp->msgs[0].pid, "水球丟過去： ",
                     currutmp->msgs[0].userid, WATERBALL_GENERAL, NULL);
            break;
          case MSGMODE_FROMANGEL:
            my_write(currutmp->msgs[0].pid, "再問一次： ",
                     currutmp->msgs[0].userid, WATERBALL_ANGEL, NULL);
            break;
          case MSGMODE_TOANGEL:
            my_write(currutmp->msgs[0].pid, "回答小主人： ",
                     currutmp->msgs[0].userid, WATERBALL_ANSWER, NULL);
            break;
        }
#endif
        vkey_attach(my_newfd);

        /* 還原螢幕 */
        scr_restore(&old_screen);
        return KEY_INCOMPLETE;
      }
      break;

    case KEY_TAB:
      if (watermode <= 0 ||
          (!PAGER_UI_IS(PAGER_UI_ORIG) || PAGER_UI_IS(PAGER_UI_NEW)))
        break;

      check_water_init();
      watermode = (watermode + water_which->count)
          % water_which->count + 1;
      t_display_new();
      return KEY_INCOMPLETE;

    case Ctrl('T'):
      if (watermode <= 0 ||
          !(PAGER_UI_IS(PAGER_UI_ORIG) || PAGER_UI_IS(PAGER_UI_NEW)))
        break;

      check_water_init();
      if (watermode > 1)
        watermode--;
      else
        watermode = water_which->count;
      t_display_new();
      return KEY_INCOMPLETE;

    case Ctrl('F'):
      if (watermode <= 0 || !PAGER_UI_IS(PAGER_UI_NEW))
        break;

      check_water_init();
      water_which_flag =
          (water_which_flag + 1) % (int)(water_usies + 1);
      if (water_which_flag == 0)
        water_which = &water[0];
      else
        water_which = swater[water_which_flag - 1];
      watermode = 1;
      t_display_new();
      return KEY_INCOMPLETE;

    case Ctrl('G'):
      if (watermode <= 0 || !PAGER_UI_IS(PAGER_UI_NEW))
        break;

      check_water_init();
      water_which_flag = (water_which_flag + water_usies) %
          (water_usies + 1);
      if (water_which_flag == 0)
        water_which = &water[0];
      else
        water_which = swater[water_which_flag - 1];

      watermode = 1;
      t_display_new();
      return KEY_INCOMPLETE;
  }
  return ch;
}

#ifndef USE_NIOS

/* ----------------------------------------------------- */
/* input routines                                        */
/* ----------------------------------------------------- */

// traditional implementation

static int    i_newfd = 0;
static struct timeval i_to, *i_top = NULL;

static inline void
add_io(int fd, int timeout)
{
  i_newfd = fd;
  if (timeout) {
    i_to.tv_sec = timeout;
    i_to.tv_usec = 16384;	/* Ptt: 改成16384 避免不按時for loop吃cpu
				 * time 16384 約每秒64次 */
    i_top = &i_to;
  } else
    i_top = NULL;
}

static inline int
num_in_buf(void)
{
  return vbuf_size(pvin);
}

static inline void
drop_input(void)
{
  vbuf_clear(pvin);
}

ssize_t put_vin(unsigned char *buf, ssize_t len) {
  fprintf(stderr, "[mock_io.put_vin] start: buf: len: %lu\n", len);
  for(int i = 0; i < len; i++) {
    if(i && i % 10 == 0) {
      fprintf(stderr, "\n");
    }
    fprintf(stderr, " %02X", buf[i]);
  }
  fprintf(stderr, "\n");

#ifdef CONVERT
  len = convert_read(pvin, buf, len);
#else
  len = vbuf_putblk(pvin, buf, len);
#endif
  fprintf(stderr, "[mock_io.put_vin] to return: buf: len (ret): %lu\n", len);
  return len;
}

/* returns:
 * >0 if read something
 * =0 if nothing read
 * <0 if need to read again
 */
static inline ssize_t
read_vin() {
  assert(!"[mock_io.read_vin] SHOULD NOT BE HERE!\n");
  return 0;
}

/*
 * dogetch() is not reentrant-safe. SIGUSR[12] might happen at any time, and
 * dogetch() might be called again, and then input buffer state may be
 * inconsistent. We try to not segfault here...
 */

static int
dogetch(void)
{
  ssize_t         len;
  static time4_t  lastact;

  size_t buf_size = vbuf_size(pvin);
  fprintf(stderr, "[mock_io.dogetch] start: buf_size: %zu to refresh\n", buf_size);

  // XXX do refresh because never vbuf_is_empty.
  refresh();

  while (vbuf_is_empty(pvin)) {
    refresh();

    if (i_newfd) {

      struct timeval  timeout;
      fd_set          readfds;

      if (i_top)
        timeout = *i_top;	/* copy it because select() might
                                 * change it */

      FD_ZERO(&readfds);
      FD_SET(0, &readfds);
      FD_SET(i_newfd, &readfds);

      /* jochang: modify first argument of select from FD_SETSIZE */
      /* since we are only waiting input from fd 0 and i_newfd(>0) */

      STATINC(STAT_SYSSELECT);
      while ((len = select(i_newfd + 1, &readfds, NULL, NULL,
                           i_top ? &timeout : NULL)) < 0) {
        if (errno != EINTR)
          abort_bbs(0);
        /* raise(SIGHUP); */
      }

      if (len == 0){
        syncnow();
        return I_TIMEOUT;
      }

      if (i_newfd && FD_ISSET(i_newfd, &readfds)){
        syncnow();
        return I_OTHERDATA;
      }
    }

#ifdef NOKILLWATERBALL
    if( currutmp && currutmp->msgcount && !reentrant_write_request )
      write_request(1);
#endif // NOKILLWATERBALL

    STATINC(STAT_SYSREADSOCKET);

    do {
      len = read_vin();
      // warning: len is 1/0/-1 now, not real length.
    } while (len <= 0);
  }

  if (currutmp) {
    syncnow();
    /* 3 秒內超過兩 byte 才算 active, anti-antiidle.
     * 不過方向鍵等組合鍵不止 1 byte */
    if (now - lastact < 3)
      currutmp->lastact = now;
    lastact = now;
  }

  // see vtkbd.c for CR/LF Rules
  assert(!vbuf_is_empty(pvin));
  {
    unsigned char c = vbuf_pop(pvin);
    // CR LF are treated as one.
    if (c == KEY_CR)
    {
      // peak next character. (peek return EOF for empty)
      if (vbuf_peek(pvin) == KEY_LF)
        vbuf_pop(pvin);
      return KEY_ENTER;
    }
    else if (c == KEY_LF)
    {
      return KEY_UNKNOWN;
    }

    return c;
  }
}

// virtual terminal keyboard context
static VtkbdCtx vtkbd_ctx;

static inline int
igetch(void)
{
  register int ch;

  while (1)
  {
    ch = dogetch();

    // convert virtual terminal keys
    ch = vtkbd_process(ch, &vtkbd_ctx);
    switch(ch)
    {
      case KEY_INCOMPLETE:
        // XXX what if endless?
        fprintf(stderr, "[mock_io.igetch]: KEY_INCOMPLETE\n");
        continue;

      case KEY_ESC:
        KEY_ESC_arg = vtkbd_ctx.esc_arg;
        fprintf(stderr, "[mock_io.igetch]: KEY_ESC: esc_arg: %d\n", KEY_ESC_arg);
        return ch;

      case KEY_UNKNOWN:
        fprintf(stderr, "[mock_io.igetch]: KEY_UNKNOWN\n");
        return ch;

        // common global hot keys...
      case Ctrl('L'):
        fprintf(stderr, "[mock_io.igetch]: to redraw\n");
        redrawwin();
        refresh();
        continue;
#ifdef DEBUG
      case Ctrl('Q'):
        {
          char usage[80];
          get_memusage(sizeof(usage), usage);
          vmsg(usage);
        }
        continue;
#endif // DEBUG
    }

    // complex pager hot keys
    if (currutmp)
    {
      ch = process_pager_keys(ch);
      fprintf(stderr, "[mock_io.igetch]: after currutmp.process_pager_keys: ch: %d KEY_INCOMPLETE: %d\n", ch, KEY_INCOMPLETE);
      if (ch == KEY_INCOMPLETE)
        continue;
    }

    fprintf(stderr, "[mock_io.igetch: to return: ch: %d\n", ch);

    return ch;
  }
  // should not reach here. just to make compiler happy.
  return ch;
}

/*
 * wait user input anything for f seconds.
 * if f == 0, return immediately
 * if f < 0,  wait forever.
 * Return 1 if anything available.
 */
static inline int
wait_input(float f, int bIgnoreBuf)
{
#pragma unused(f, bIgnoreBuf)
  return 0;
}

/* nios vkey system emulation */

inline int
vkey_is_ready(void)
{
  return num_in_buf() > 0;
}

inline int
vkey_is_typeahead()
{
  return num_in_buf() > 0;
}

inline int
vkey_is_full(void)
{
  return vbuf_is_full(pvin);
}

inline void
vkey_purge(void)
{
  int max_try = 64;
  drop_input();

  STATINC(STAT_SYSREADSOCKET);
  while (wait_input(0.01, 1) && max_try-- > 0) {
    read_vin();
    drop_input();
  }
}

int
vkey_attach(int fd)
{
  int r = i_newfd;
  add_io(fd, 0);
  return r;
}

int
vkey_detach(void)
{
  int r = i_newfd;
  add_io(0, 0);
  return r;
}

inline int
vkey(void)
{
  return igetch();
}

inline int
vkey_poll(int ms)
{
  if (ms) refresh();
  // XXX handle I_OTHERDATA?
  return wait_input(ms / (double)MILLISECONDS, 0);
}

int
vkey_prefetch(int timeout) {
  if (wait_input(timeout / (double)MILLISECONDS, 1) && !vbuf_is_full(pvin))
    read_vin();
  return num_in_buf() > 0;
}

int
vkey_is_prefetched(char c) {
  // only ^x keys are safe to be detected.
  // other keys may fall into escape sequence.
  assert ((int)c == EOF || (c > 0 && c < ' '));

  if ((int)c == EOF)
    return 0;

  return vbuf_strchr(pvin, c) >= 0 ? 1 : 0;
}

#endif // USE_NIOS
