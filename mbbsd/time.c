#ifdef __dietlibc__
#include <time.h>
#warning	"hardcoded time zone as GMT+8!"
extern void __maplocaltime(void);
extern time_t __tzfile_map(time_t t, int *isdst, int forward);
extern time_t timegm(struct tm *const t);

time_t mktime(register struct tm* const t) {
  time_t x=timegm(t);
  x-=8*3600;
  return x;
}

struct tm* localtime_r(const time_t* t, struct tm* r) {
  time_t tmp;
  tmp=*t;
  tmp+=8*3600;
  return gmtime_r(&tmp,r);
}
#endif
