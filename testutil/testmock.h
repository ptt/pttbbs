#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
  // mock-io
  //kcwu: 80x24 一般使用者名單 1.9k, 含 header 2.4k
  // 一般文章推文頁約 2590 bytes
  // 注意實際可用的空間為 N-1。
  #define OBUFSIZE 3072
  #define IBUFSIZE 2048

  extern VBUF vout, *pvout;
  extern VBUF vin, *pvin;
  extern unsigned char OFLUSH_BUF[OBUFSIZE*5];
  extern unsigned char *pOFLUSH_BUF;

  void reset_oflush_buf();

  void log_oflush_buf();

  ssize_t put_vin(unsigned char *buf, ssize_t len);

#ifdef __cplusplus
}
#endif // __cplusplus
