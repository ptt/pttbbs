/* $Id$ */
#include "bbs.h"

#error "Not complete yet"

aidu_t fn2aidu(char *fn)
{
  aidu_t aidu = 0;
  aidu_t type = 0;
  aidu_t v1 = 0;
  aidu_t v2 = 0;
  char *sp = fn;

  if(fn == NULL)
    return 0;

  switch(*(sp ++))
  {
    case 'M':
      type = 0;
      break;
    case 'G':
      type = 1;
      break;
    default:
      return 0;
      break;
  }

  if(*(sp ++) != '.')
    return 0;
  v1 = strtoul(sp, &sp, 10);
  if(sp == NULL)
    return 0;
  if(*sp != '.' || *(sp + 1) != 'A')
    return 0;
  sp += 2;
  if(*(sp ++) == '.')
  {
    v2 = strtoul(sp, &sp, 16);
    if(sp == NULL)
      return 0;
  }
  aidu = ((type & 0xf) << 44) | ((v1 & 0xffffffff) << 12) | (v2 & 0xfff);

  return aidu;
}

/* IMPORTANT:
 *   size of buf must be at least 8+1 bytes
 */
char *aidu2aidc(char *buf, aidu_t aidu)
{
  const char aidu2aidc_table[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
  const int aidu2aidc_tablesize = sizeof(aidu2aidc_table) - 1;
  char *sp = buf + 8;
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
char *aidu2fn(char *fn, aidu_t aidu)
{
  aidu_t type = ((aidu >> 44) & 0xf);
  aidu_t v1 = ((aidu >> 12) & 0xffffffff);
  aidu_t v2 = (aidu & 0xfff);

  if(fn == NULL)
    return NULL;
  snprintf(fn, FNLEN - 1, "%c.%d.A.%03X", ((type == 0) ? 'M' : 'G'), (unsigned int)v1, (unsigned int)v2);
  fn[FNLEN - 1] = '\0';
  return fn;
}

aidu_t aidc2aidu(char *aidc)
{
  char *sp = aidc;
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
    else if(*sp == '@')
      break;
    else
      return 0;
    aidu <<= 6;
    aidu |= (v & 0x3f);
    sp ++;
  }

  return aidu;
}

int search_aidu_in_bfile(char *bfile, aidu_t aidu)
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

SearchAIDResult_t search_aidu_in_board(char *bname, aidu_t aidu)
{
  SearchAIDResult_t r = {AIDR_BOARD, -1}:
  int n = -1;
  char dirfile[PATHLEN];

  {
    char bf[FNLEN];

    snprintf(bf, FNLEN, "%s.bottom", FN_DIR);
    setbfile(dirfile, bname, bf);
    if((n = search_aidu_in_bfile(dirfile, aidu)) >= 0)
    {
      r.where = AIDR_BOTTOM;
      r.n = n;
    }
  }
  if(r.n < 0)
  {
    setbfile(dirfile, bname, FN_DIR);
    if((n = search_aidu_in_bfile(dirfile, aidu)) >= 0)
    {
      r.where = AIDR_BOARD;
      r.n = n;
    }
  }
  if(r.n < 0)
  {
    setbfile(dirfile, bname, fn_mandex);
    if((n = search_aidu_in_bfile(dirfile, aidu)) >= 0)
    {
      r.where = AIDR_DIGEST;
      r.n = n;
    }
  }
  return r;
}

SearchAIDResult_t do_search_aid(void)
{
  SearchAIDResult_t r = {AIDR_BOARD, -1};
  char aidc[100];
  char bname[IDLEN + 1] = "";
  aidu_t aidu = 0;
  char *sp;
  char *sp2;
  char *emsg = NULL;

  if(!getdata(b_lines, 0, "搜尋" AID_DISPLAYNAME ": #", aidc, 15 + IDLEN, LCECHO))
  {
    move(b_lines, 0);
    clrtorol();
    r.n = -1;
    return r;
  }

  if(currstat == RMAIL)
  {
    move(21, 0);
    clrtobot();
    move(22, 0);
    prints("此狀態下無法搜尋" AID_DISPLAYNAME);
    pressanykey();
    r.n = -1;
    return r;
  }

  sp = aidc;
  while(*sp == ' ')
    sp ++;
  while(*sp == '#')
    sp ++;
  aidu = aidc2aidu(sp);
  if((sp2 = strchr(sp, '@')) != NULL)
  {
    strncpy(bname, sp2 + 1, IDLEN);
    bname[IDLEN] = '\0';
    *sp2 = '\0';
  }
  else
    bname[0] = '\0';

  if(aidu > 0)
  {
    if(bname[0] != '\0')
    {
      if(!HasBoardPerm_bn(bname))
        return FULLUPDATE;
      r = search_aidu_in_board(bname, aidu);
      if(r.n >= 0)
      {
        if(enter_board(bname) < 0)
        {
          r.n = -1;
          emsg = "錯誤：無法進入指定的看板 %s";
        }
      }
    }
    else
    {
      r = search_aidu_in_board(currboard, aidu);
    }
  }

  if(r.n < 0)
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
    r.n = -1;
    return r;
  }
  else
  {
    return r;
  }
}
