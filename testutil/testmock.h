#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

  // mock-io
  #define OBUFSIZE 3072
  #define IBUFSIZE 128

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
