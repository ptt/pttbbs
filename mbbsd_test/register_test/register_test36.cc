#include "register_test.h"

TEST_F(RegisterTest, MRegister) {
  char buf[8192] = {};

  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[register_test35] after initcuser\n");

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  sprintf(buf, "y\r\ntest check2\r\ntestcareer2\r\ntestaddr1234567\xA5\xAB" "02\r\ny\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  EXPECT_EQ(cuser.userlevel & PERM_LOGINOK, 0);

  u_manual_verification();

  refresh();
  reset_oflush_buf();

  // load SYSOP10
  initcuser("SYSOP10");
  fprintf(stderr, "[register_test35] after initcuser\n");

  refresh();
  reset_oflush_buf();

  sprintf(buf, "y\r\ntest check10\r\ntestcareer10\r\ntestaddr1234567\xA5\xAB" "10\r\ny\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  EXPECT_EQ(cuser.userlevel & PERM_LOGINOK, 0);

  u_manual_verification();

  refresh();
  reset_oflush_buf();

  // load SYSOP11
  initcuser("SYSOP11");
  fprintf(stderr, "[register_test35] after initcuser\n");

  refresh();
  reset_oflush_buf();

  sprintf(buf, "y\r\ntest check11\r\ntestcareer11\r\ntestaddr1234567\xA5\xAB" "11\r\ny\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  EXPECT_EQ(cuser.userlevel & PERM_LOGINOK, 0);

  u_manual_verification();

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  sprintf(buf, "e\r\nyjyjy  \r\n  ");
  put_vin((unsigned char *)buf, strlen(buf));

  m_register();

  refresh();

  unsigned char expected[] = ""
      "\x1B[H\x1B[J"
      "\x1B[1;37;46m\xA1i \xBC" "f\xAE\xD6\xA8\xCF\xA5\xCE\xAA\xCC\xB5\xF9\xA5U\xB8\xEA\xAE\xC6 \xA1j" //�i �f�֨ϥΪ̵��U��� �j
      "\x1B[m\x1B[3;1HSYSOP2\n\r"
      "SYSOP10\n\r"
      "SYSOP11"
      "\x1B[23;1H\xB6}\xA9l\xBC" "f\xAE\xD6\xB6\xDC (Y:\xB3\xE6\xB5\xA7\xBC\xD2\xA6\xA1/N:\xA4\xA3\xBC" "f/E:\xBE\xE3\xAD\xB6\xBC\xD2\xA6\xA1/U:\xAB\xFC\xA9wID)\xA1H[N] " //�}�l�f�ֶ� (Y:�浧�Ҧ�/N:���f/E:�㭶�Ҧ�/U:���wID)�H[N]
      "\x1B[0;7m    \x1B[m"
      "\x1B[23;57H\r\xB6}\xA9l\xBC" "f\xAE\xD6\xB6\xDC (Y:\xB3\xE6\xB5\xA7\xBC\xD2\xA6\xA1/N:\xA4\xA3\xBC" "f/E:\xBE\xE3\xAD\xB6\xBC\xD2\xA6\xA1/U:\xAB\xFC\xA9wID)\xA1H[N] "
      "\x1B[0;7me   \x1B[m"
      "\x1B[23;58H\x1B[H\x1B[J"
      ">  1. \x1B[1mSYSOP2       \x1B[m"
      "\x1B[1;31m        test check2 "
      "\x1B[1;32mtestcareer2                             \x1B[m\n\r"
      "      testaddr1234567\xA5\xAB" "02                               "
      "\x1B[0;33m127.0.0.1\x1B[m\n\r"
      "   2. \x1B[1mSYSOP10      \x1B[m"
      "\x1B[1;31m       test check10 "
      "\x1B[1;32mtestcareer10                            \x1B[m\n\r"
      "      testaddr1234567\xA5\xAB" "10                               "
      "\x1B[0;33m172.19.0.1\x1B[m\n\r"
      "   3. \x1B[1mSYSOP11      \x1B[m"
      "\x1B[1;31m       test check11 "
      "\x1B[1;32mtestcareer11                            \x1B[m\n\r"
      "      testaddr1234567\xA5\xAB" "11                               "
      "\x1B[0;33m172.19.0.1\x1B[m\n\r"
      "\x1B[7m                                                             \xA4w\xC5\xE3\xA5\xDC 3 \xA5\xF7\xB5\xF9\xA5U\xB3\xE6 \x1B[m" //                                                             �w��� 3 �����U��
      "\x1B[24;1H\x1B[0;34;46m \xBC" "f\xAE\xD6 " // �f��
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(y)"
      "\x1B[0;30;47m\xB1\xB5\xA8\xFC" //����
      "\x1B[0;31;47m(n)"
      "\x1B[0;30;47m\xA9\xDA\xB5\xB4" //�ڵ�
      "\x1B[0;31;47m(d)"
      "\x1B[0;30;47m\xA5\xE1\xB1\xBC " //�ᱼ
      "\x1B[0;31;47m(s)"
      "\x1B[0;30;47m\xB8\xF5\xB9L" //���L
      "\x1B[0;31;47m(u)"
      "\x1B[0;30;47m\xB4_\xAD\xEC " //�_��
      "\x1B[0;31;47m(\xAA\xC5\xA5\xD5/PgDn)" //(�ť�/PgDn)
      "\x1B[0;30;47m\xC0x\xA6s+\xA4U\xAD\xB6 " //�x�s+�U��
      "\x1B[0;31;47m(q/END)"
      "\x1B[0;30;47m\xB5\xB2\xA7\xF4   " //����
      "\x1B[m\x1B[1;1H\x1B[1;30;40m   1. SYSOy2               test check2 testcareer2                             \x1B[m\x1B[K\n\r"
      "\x1B[1;30;40m      testaddr1234567\xA5\xAB" "02                               127.0.0.1\x1B[m\x1B[K\n\r"
      ">\r"
      " \x1B[5;1H>\r"
      ">[1;30;40m   3. SYSOy11             test check11 testcareer11                            \x1B[m\x1B[K\n\r"
      "\x1B[1;30;40m      testaddr1234567\xA5\xAB" "11                               172.19.0.1\x1B[m\x1B[K\x1B[5;1H>\r"
      ">[1;30;40m "
      "[1;30;40my  3. SYSOy11             test check11 testcareer11                            \x1B[m\x1B[K\n\r"
      "\x1B[1;30;40m      testaddr1234567\xA5\xAB" "11                               172.19.0.1\x1B[m\x1B[K"
      "\x1B[5;1H "
      "\x1B[24;1H\xA9|\xA5\xBC\xAB\xFC\xA9w\xAA\xBA 1 \xAD\xD3\xB6\xB5\xA5\xD8\xADn: (S\xB8\xF5\xB9L/y\xB3q\xB9L/n\xA9\xDA\xB5\xB4/e\xC4~\xC4\xF2\xBDs\xBF\xE8): " //�|�����w�� 1 �Ӷ��حn: (S���L/y�q�L/n�ڵ�/e�~��s��):
      "\x1B[0;7m   \x1B[m\x1B[K"
      "\x1B[24;55H\r\xA9|\xA5\xBC\xAB\xFC\xA9w\xAA\xBA 1 \xAD\xD3\xB6\xB5\xA5\xD8\xADn: (S\xB8\xF5\xB9L/y\xB3q\xB9L/n\xA9\xDA\xB5\xB4/e\xC4~\xC4\xF2\xBDs\xBF\xE8): "
      "\x1B[0;7m   \x1B[m\x1B[24;56H\x1B[H\x1B[J\x1B[6;1H"
      "\xB1z\xC0\xCB\xB5\xF8\xA4" "F 3 \xA5\xF7\xB5\xF9\xA5U\xB3\xE6\xA1" "C" //�z�˵��F 3 �����U��C
      "\x1B[24;1H\x1B[1;34;44m \xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e" // �e�e�e�e�e�e�e�e�e�e�e�e�e�e�e
      "\x1B[1;37;44m \xBD\xD0\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2 " // �Ы����N���~��
      "\x1B[1;34;44m\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e  \x1B[m\r\x1B[K"
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
