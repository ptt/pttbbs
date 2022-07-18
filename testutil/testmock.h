#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
  // mock-io
  //kcwu: 80x24 �@��ϥΪ̦W�� 1.9k, �t header 2.4k
  // �@��峹���孶�� 2590 bytes
  // �`�N��ڥi�Ϊ��Ŷ��� N-1�C
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
