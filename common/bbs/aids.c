#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmsys.h"
#include "cmbbs.h"
#include "pttstruct.h"
#include "common.h"
#include "config.h"

static inline int
match_fhdr_stamp_hex(const fileheader_t *fh, char prefix_ch,
                     time4_t target_ts, unsigned int target_hex,
                     int allow_prefix, int required_mode)
{
    if (!fh->filename[0] || fh->filename[0] == '.')
        return 0;
    /* A locked pinned post (M -> L) must stay pinned. */
    if (prefix_ch && fh->filename[0] != prefix_ch &&
        !(prefix_ch == 'M' && fh->filename[0] == 'L' &&
          (required_mode & FILE_BOTTOM)))
        return 0;
    if (required_mode) {
        if (fh->owner[0] == '-')
            return 0;
        if ((fh->filemode & required_mode) != required_mode)
            return 0;
    }
    if (get_fhdr_stamp_ts(fh->filename) != target_ts)
        return 0;
    unsigned int hex = get_fhdr_stamp_hex(fh->filename);
    if (hex == target_hex) {
        if (target_hex != 0 || allow_prefix)
            return 1;
        const char *dot = strrchr(fh->filename, '.');
        return (dot && (dot[1] == '0' || (dot[1] == 'A' && dot[2] == '\0')));
    }
    return 0;
}

int
search_dir_by_stamp_fd(int fd, int total, int hint_idx, char prefix_ch,
                       time4_t target_ts, unsigned int target_hex,
                       int allow_prefix, int required_mode,
                       fileheader_t *out_fh)
{
    fileheader_t fh;

    if (total <= 0)
        return 0;

    /* Stage 1: O(1) fast path at hint_idx */
    if (hint_idx >= 1 && hint_idx <= total) {
        if (pread(fd, &fh, sizeof(fh), (off_t)(hint_idx - 1) * sizeof(fh)) == (ssize_t)sizeof(fh)) {
            if (match_fhdr_stamp_hex(&fh, prefix_ch, target_ts, target_hex, allow_prefix, required_mode)) {
                if (out_fh) {
                    *out_fh = fh;
                    if (NEED_STORAGE_CONV)
                        fileheader_storage_to_mem(out_fh, 1);
                }
                return hint_idx;
            }
        }
    }

    /* Stage 2: Local shift window [hint_idx - 32 .. hint_idx + 4] */
    if (hint_idx >= 1) {
        int start = hint_idx - 32;
        int end = hint_idx + 4;
        if (start < 1)
            start = 1;
        if (end > total)
            end = total;
        if (start <= end) {
            fileheader_t buf[40];
            int cnt = end - start + 1;
            ssize_t n = pread(fd, buf, (size_t)cnt * sizeof(fileheader_t),
                              (off_t)(start - 1) * sizeof(fileheader_t));
            if (n > 0) {
                cnt = (int)(n / sizeof(fileheader_t));
                for (int i = cnt - 1; i >= 0; i--) {
                    if (match_fhdr_stamp_hex(&buf[i], prefix_ch, target_ts, target_hex, allow_prefix, required_mode)) {
                        if (out_fh) {
                            *out_fh = buf[i];
                            if (NEED_STORAGE_CONV)
                                fileheader_storage_to_mem(out_fh, 1);
                        }
                        return start + i;
                    }
                }
            }
        }
    }

    /* Stage 3: Timestamp binary search across .DIR + window scan */
    enum { BSEARCH_WIN = 256 };
    if (total > BSEARCH_WIN) {
        int low = 1, high = total, mid = 1;
        while (low <= high) {
            mid = low + (high - low) / 2;
            if (pread(fd, &fh, sizeof(fh), (off_t)(mid - 1) * sizeof(fh)) != (ssize_t)sizeof(fh))
                break;
            time4_t ts = get_fhdr_stamp_ts(fh.filename);
            if (ts == target_ts)
                break;
            else if (ts < target_ts)
                low = mid + 1;
            else
                high = mid - 1;
        }

        int wstart = mid - BSEARCH_WIN / 2;
        if (wstart < 1)
            wstart = 1;
        if (wstart + BSEARCH_WIN - 1 > total)
            wstart = total - BSEARCH_WIN + 1;
        if (wstart < 1)
            wstart = 1;

        fileheader_t win_buf[BSEARCH_WIN];
        int wcnt = total - wstart + 1;
        if (wcnt > BSEARCH_WIN)
            wcnt = BSEARCH_WIN;
        ssize_t n = pread(fd, win_buf, (size_t)wcnt * sizeof(fileheader_t),
                          (off_t)(wstart - 1) * sizeof(fileheader_t));
        if (n > 0) {
            wcnt = (int)(n / sizeof(fileheader_t));
            for (int i = wcnt - 1; i >= 0; i--) {
                if (match_fhdr_stamp_hex(&win_buf[i], prefix_ch, target_ts, target_hex, allow_prefix, required_mode)) {
                    if (out_fh) {
                        *out_fh = win_buf[i];
                        if (NEED_STORAGE_CONV)
                            fileheader_storage_to_mem(out_fh, 1);
                    }
                    return wstart + i;
                }
            }
        }
    }

    /* Stage 4: Backwards batched linear scan fallback (handles out-of-order .DIR) */
    enum { LINEAR_BATCH = 256 };
    fileheader_t batch[LINEAR_BATCH];
    for (int end_rec = total; end_rec >= 1; end_rec -= LINEAR_BATCH) {
        int start_rec = end_rec - LINEAR_BATCH + 1;
        if (start_rec < 1)
            start_rec = 1;
        int cnt = end_rec - start_rec + 1;
        ssize_t n = pread(fd, batch, (size_t)cnt * sizeof(fileheader_t),
                          (off_t)(start_rec - 1) * sizeof(fileheader_t));
        if (n <= 0)
            break;
        cnt = (int)(n / sizeof(fileheader_t));
        for (int i = cnt - 1; i >= 0; i--) {
            if (match_fhdr_stamp_hex(&batch[i], prefix_ch, target_ts, target_hex, allow_prefix, required_mode)) {
                if (out_fh) {
                    *out_fh = batch[i];
                    if (NEED_STORAGE_CONV)
                        fileheader_storage_to_mem(out_fh, 1);
                }
                return start_rec + i;
            }
        }
    }
    return 0;
}

int
search_dir_by_aidu_fd(int fd, int total, aidu_t aidu,
                      int required_mode, fileheader_t *out_fh)
{
    if (aidu_raw(aidu) == 0)
        return 0;
    int hint_idx = aidu_idx(aidu);
    time4_t target_ts = aidu_stamp(aidu);
    unsigned int target_hex = aidu_hex(aidu);
    int allow_prefix = (target_hex == 0 && !(required_mode & FILE_BOTTOM));
    char prefix_ch = aidu_type(aidu) ? 'G' : 'M';
    return search_dir_by_stamp_fd(fd, total, hint_idx, prefix_ch,
                                  target_ts, target_hex,
                                  allow_prefix, required_mode, out_fh);
}

typedef struct {
    uint32_t magic;
    int32_t  bid;
    uint64_t aidu;
    int32_t  required_mode;
    char     direct[256];
} PACKSTRUCT search_aid_req_hdr_t;

typedef struct {
    int32_t      status;
    int32_t      found_idx;
    fileheader_t fh;
} PACKSTRUCT search_aid_resp_t;

static int
search_aidu_via_svc(const char *direct, int bid, aidu_t aidu,
                    int required_mode, fileheader_t *out_fh)
{
    int sfd = search_svc_connect();
    if (sfd < 0)
        return -1;

    search_aid_req_hdr_t req;
    memset(&req, 0, sizeof(req));
    req.magic = SEARCH_AID_MAGIC;
    req.bid = bid;
    req.aidu = aidu;
    req.required_mode = required_mode;
    strlcpy(req.direct, direct, sizeof(req.direct));

    if (search_svc_io(sfd, &req, sizeof(req), 1) != (int)sizeof(req)) {
        close(sfd);
        return -1;
    }

    search_aid_resp_t resp;
    if (search_svc_io(sfd, &resp, sizeof(resp), 0) != (int)sizeof(resp) || resp.status != 0) {
        close(sfd);
        return -1;
    }
    close(sfd);

    if (resp.found_idx > 0 && out_fh)
        *out_fh = resp.fh;
    return resp.found_idx;
}

int
search_dir_by_aidu(const char *direct, aidu_t aidu,
                   int required_mode, fileheader_t *out_fh)
{
    if (!direct || aidu_raw(aidu) == 0)
        return 0;

    int fd = open(direct, O_RDONLY);
    if (fd < 0)
        return 0;

    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < (off_t)sizeof(fileheader_t)) {
        close(fd);
        return 0;
    }
    int total = (int)(st.st_size / sizeof(fileheader_t));

    if (aidu_idx(aidu) == 0) {
        int svc_idx = search_aidu_via_svc(direct, 0, aidu, required_mode, out_fh);
        if (svc_idx == 0) {
            close(fd);
            return 0;
        }
        if (svc_idx > 0) {
            fileheader_t vfh;
            time4_t target_ts = aidu_stamp(aidu);
            unsigned int target_hex = aidu_hex(aidu);
            int allow_prefix = (target_hex == 0 && !(required_mode & FILE_BOTTOM));
            char prefix_ch = aidu_type(aidu) ? 'G' : 'M';
            if (svc_idx <= total &&
                pread(fd, &vfh, sizeof(vfh), (off_t)(svc_idx - 1) * sizeof(vfh)) == (ssize_t)sizeof(vfh) &&
                match_fhdr_stamp_hex(&vfh, prefix_ch, target_ts, target_hex, allow_prefix, required_mode)) {
                if (out_fh) {
                    *out_fh = vfh;
                    if (NEED_STORAGE_CONV)
                        fileheader_storage_to_mem(out_fh, 1);
                }
                close(fd);
                return svc_idx;
            }
            /* Mismatch detected: search.svc cache is stale. Invalidate it and
             * fall through to the authoritative local scan (a re-query could
             * still return an unverified, stale index). */
            search_svc_invalidate(direct, 0);
        }
    }

    int found = search_dir_by_aidu_fd(fd, total, aidu, required_mode, out_fh);
    close(fd);
    return found;
}


aidu_t
fn2aidu(const char *fn)
{
    if (!fn || (fn[0] != 'M' && fn[0] != 'G') || fn[1] != '.')
        return 0;
    time4_t ts = get_fhdr_stamp_ts(fn);
    if (ts <= 0)
        return 0;
    const char *dot_a = strstr(fn + 2, ".A");
    if (!dot_a || (dot_a[2] != '\0' && dot_a[2] != '.'))
        return 0;
    unsigned int hex = get_fhdr_stamp_hex(fn);
    aidu_t raw = aidu_pack(0, ts, hex);
    if (fn[0] == 'G')
        raw |= AIDU_TYPE_G;
    return raw;
}

/* IMPORTANT:
 *   size of buf must be at least 8+1 bytes
 */
char *aidu2aidc(char *buf, const aidu_t orig_aidu)
{
  const char aidu2aidc_table[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
  const int aidu2aidc_tablesize = sizeof(aidu2aidc_table) - 1;
  char *sp = buf + 8;
  aidu_t aidu = aidu_raw(orig_aidu);
  aidu_t v;

  *(sp --) = '\0';
  while(sp >= buf)
  {
    /* FIXME: 能保證 aidu2aidc_tablesize 是 2 的冪次的話，
              這裡可以改用 bitwise operation 做 */
    v = aidu % aidu2aidc_tablesize;
    aidu = aidu / aidu2aidc_tablesize;
    *(sp --) = aidu2aidc_table[v];
  }
  return buf;
}

/* IMPORTANT:
 *   size of fn must be at least FNLEN bytes
 */
char *aidu2fn(char *fn, const aidu_t aidu)
{
  int type = aidu_type(aidu);
  aidu_t v1 = ((aidu >> 12) & 0xffffffff);
  aidu_t v2 = (aidu & 0xfff);

  if(fn == NULL)
    return NULL;
  snprintf(fn, FNLEN, "%c.%d.A.%03X", ((type == 0) ? 'M' : 'G'), (unsigned int)v1, (unsigned int)v2);
  return fn;
}

aidu_t aidc2aidu(const char *aidc)
{
  const char *sp = aidc;
  aidu_t aidu = 0;

  if(aidc == NULL)
    return 0;

  while(*sp != '\0' && /* ignore trailing spaces */ *sp != ' ')
  {
    aidu_t v = 0;
    /* FIXME: 查表法會不會比較快？ */
    if(*sp >= '0' && *sp <= '9')
      v = *sp - '0';
    else if(*sp >= 'A' && *sp <= 'Z')
      v = *sp - 'A' + 10;
    else if(*sp >= 'a' && *sp <= 'z')
      v = *sp - 'a' + 36;
    else if(*sp == '-')
      v = 62;
    else if(*sp == '_')
      v = 63;
#ifdef NEW_AIDS
    else if(*sp == '@')
      break;
#endif
    else
      return 0;
    aidu <<= 6;
    aidu |= (v & 0x3f);
    sp ++;
  }

  return aidu;
}

int
search_aidu(char *bfile, aidu_t aidu)
{
    int idx = search_dir_by_aidu(bfile, aidu, 0, NULL);
    return idx > 0 ? idx - 1 : -1;
}

#ifdef NEW_AIDS
int search_aidu_in_bfile(const char *bfile, const aidu_t aidu)
{
  char fn[FNLEN];
  int fd;
  fileheader_t fhs[64];
  int len, i;
  int pos = 0;
  int found = 0;
  int lastpos = 0;

  if(aidu2fn(fn, aidu) == NULL)
    return -1;
  if((fd = open(bfile, O_RDONLY, 0)) < 0)
    return -1;

  while(!found && (len = read(fd, fhs, sizeof(fhs))) > 0)
  {
    len /= sizeof(fileheader_t);
    for(i = 0; i < len; i ++)
    {
      int l;
      if(strcmp(fhs[i].filename, fn) == 0 ||
         ((aidu & 0xfff) == 0 && (l = strlen(fhs[i].filename)) > 6 &&
          strncmp(fhs[i].filename, fn, l) == 0))
      {
        if(fhs[i].filemode & FILE_BOTTOM)
        {
          lastpos = pos;
        }
        else
        {
          found = 1;
          break;
        }
      }
      pos ++;
    }
  }
  close(fd);

  return (found ? pos : (lastpos ? lastpos : -1));
}

int search_aidu_in_board(SearchAIDResult_t *r, const char *bname, const aidu_t aidu)
{
  int n = -1;
  char dirfile[PATHLEN];

  if(r == NULL)
    return -1;
  r->n = -1;
  /* search bottom */
  {
    char bf[FNLEN];

    snprintf(bf, FNLEN, "%s.bottom", FN_DIR);
    setbfile(dirfile, bname, bf);
    if((n = search_aidu_in_bfile(dirfile, aidu)) >= 0)
    {
      r->where = AIDR_BOTTOM;
      r->n = n;
    }
  }
  /* else search board */
  if(r->n < 0)
  {
    setbfile(dirfile, bname, FN_DIR);
    if((n = search_aidu_in_bfile(dirfile, aidu)) >= 0)
    {
      r->where = AIDR_BOARD;
      r->n = n;
    }
  }
  /* else search digest */
  if(r->n < 0)
  {
    setbfile(dirfile, bname, fn_mandex);
    if((n = search_aidu_in_bfile(dirfile, aidu)) >= 0)
    {
      r->where = AIDR_DIGEST;
      r->n = n;
    }
  }
  return r->n;
}

int do_search_aid(SearchAIDResult_t *r)
{
  char aidc[100];
  char bname[IDLEN + 1] = "";
  aidu_t aidu = 0;
  char *sp;
  char *sp2;
  char *emsg = NULL;

  if(r == NULL)
    return -1;
  r->n = -1;
  if(!getdata(b_lines, 0, "搜尋" AID_DISPLAYNAME ": #", aidc, 15 + IDLEN, LCECHO))
  {
    move(b_lines, 0);
    clrtoeol();
    return -1;
  }

  if(currstat == RMAIL)
  {
    move(21, 0);
    clrtobot();
    move(22, 0);
    prints("此狀態下無法搜尋" AID_DISPLAYNAME);
    pressanykey();
    return -1;
  }

  sp = aidc;
  while(*sp == ' ')
    sp ++;
  while(*sp == '#')
    sp ++;
  aidu = aidc2aidu(sp);
  if((sp2 = strchr(sp, '@')) != NULL)
  {
    // assert(sizeof(bname) > IDLEN);
    strlcpy(bname, sp2 + 1, IDLEN+1);
    *sp2 = '\0';
  }
  else
    bname[0] = '\0';

  if(aidu > 0)
  {
    if(bname[0] != '\0')
    {
      if(!HasBoardPerm_bn(bname))
        return -1;
      search_aidu_in_board(r, bname, aidu);
      if(r->n >= 0)
      {
        if(enter_board(bname) < 0)
        {
          r->n = -1;
          emsg = "錯誤：無法進入指定的看板 %s";
        }
      }
    }
    else
    {
      search_aidu_in_board(r, currboard, aidu);
    }
  }

  if(r->n < 0)
  {
    if(aidu == 0)
      emsg = "不合法的" AID_DISPLAYNAME "，請確定輸入是正確的";
    else if(emsg == NULL)
    {
      if(bname[0] != '\0')
        emsg = "看板 %s 內找不到這個" AID_DISPLAYNAME "，可能是文章已經消失，或是找錯看板了";
      else
        emsg = "找不到這個" AID_DISPLAYNAME "，可能是文章已經消失，或是找錯看板了";
    }
    move(21, 0);
    clrtoeol();
    move(22, 0);
    prints(emsg, bname);
    pressanykey();
    r->n = -1;
    return r->n;
  }
  else
  {
    return r->n;
  }
}
#endif
