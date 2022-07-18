#include "register_test.h"

TEST_F(RegisterTest, Regform2ValidateSingle) {
  char buf[8192] = {};
  // init SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[register_test14] after initcuser\n");

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  sprintf(buf, "y\r\ntest check\r\ntestcareer\r\ntestaddr1234567\xA5\xAB" "12\r\ny\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  EXPECT_EQ(cuser.userlevel & PERM_LOGINOK, 0);

  u_manual_verification();

  refresh();
  log_oflush_buf();
  reset_oflush_buf();


  sprintf(buf, "y\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  regform2_validate_single(NULL);

  initcuser("SYSOP2");
  EXPECT_EQ(cuser.userlevel & PERM_LOGINOK, PERM_LOGINOK);

  refresh();
  log_oflush_buf();

  unsigned char expected[] = ""
      "\x1B[2;1H        "
      "\x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s" //�r�s�r�s�r�s
      "\x1B[m  "
      "\x1B[1;30;45m    \xA8\xCF \xA5\xCE \xAA\xCC \xB8\xEA \xAE\xC6             " //    �� �� �� �� ��
      "\x1B[m  "
      "\x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m\x1B[K\n\r" //�r�s�r�s�r�s
      "        \xA5N\xB8\xB9\xBC\xCA\xBA\xD9: SYSOP2 ()\x1B[K\n\r" //        �N���ʺ�: SYSOP2 ()
      "           Ptt\xB9\xF4: 0 Ptt\xB9\xF4\n\r" //           Ptt��: 0 Ptt��
      "        \xACO\xA7_\xA6\xA8\xA6~: \xA4w\xBA\xA1" "18\xB7\xB3\x1B[K\n\r" //        �O�_���~: �w��18��
      "        \xB5\xF9\xA5U\xA4\xE9\xB4\xC1: 12/15/2020 21:53:29 Tue (\xA4w\xBA\xA1 -18611 \xA4\xD1)\x1B[K\n\r" //        ���U���: 12/15/2020 21:53:29 Tue (�w�� -18611 ��)
      "        \xA8p\xA4H\xABH\xBD" "c: 0 \xAB\xCA  (\xC1\xCA\xB6R\xABH\xBD" "c: 0 \xAB\xCA)\x1B[K\n\r" //        �p�H�H�c: 0 ��  (�ʶR�H�c: 0 ��)
      "        \xA8\xCF\xA5\xCE\xB0O\xBF\xFD: \xB5n\xA4J\xA6\xB8\xBC\xC6 25 \xA6\xB8 / \xA4\xE5\xB3\xB9 0 \xBDg\n\r" //        �ϥΰO��: �n�J���� 25 �� / �峹 0 �g
      "        \xB3\xCC\xAB\xE1\xA4W\xBDu: 06/05/2022 01:47:21 Sun (\xB1\xBE\xAF\xB8\xAE\xC9\xA8" "C\xA4\xE9\xBCW\xA5[) / 127.0.0.1\n\r" //        �̫�W�u: 06/05/2022 01:47:21 Sun (�����ɨC��W�[) / 127.0.0.1
      "        \xB0h\xA4\xE5\xBC\xC6\xA5\xD8: 0\n\r" //        �h��ƥ�: 0
      "        \xA4p \xA4\xD1 \xA8\xCF: \xB5L\n\r" //        �p �� ��: �L
      "        \x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m\n\r" //�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s
      "\xA8\xE4\xA5\xA6\xB8\xEA\xB0T: [\xA5\xBC\xB5\xF9\xA5U]" //�䥦��T: [�����U]
      "\x1B[15;1H\x1B[1;32m--------------- \xB3o\xACO\xB2\xC4  1 \xA5\xF7\xB5\xF9\xA5U\xB3\xE6 -----------------------\x1B[m\n\r" //--------------- �o�O��  1 �����U�� -----------------------
      "  \xB1" "b\xB8\xB9        : SYSOP2 \n\r" //  �b��        : SYSOP2
      "0.\xAFu\xB9\xEA\xA9m\xA6W    : test check\n\r" //0.�u��m�W    : test check
      "1.\xAA" "A\xB0\xC8\xB3\xE6\xA6\xEC    : testcareer\n\r" //1.�A�ȳ��    : testcareer
      "2.\xA5\xD8\xAB" "e\xA6\xED\xA7}    : testaddr1234567\xA5\xAB" "12" //2.�ثe��}    : testaddr1234567��12
      "\x1B[21;1H\x1B[K\x1B[24;1H\xACO\xA7_\xB1\xB5\xA8\xFC\xA6\xB9\xB8\xEA\xAE\xC6(Y/N/Q/Del/Skip)\xA1H[S] " //�O�_���������(Y/N/Q/Del/Skip)�H[S]
      "\x1B[H\x1B[J\x1B[6;1H\xB1z\xC0\xCB\xB5\xF8\xA4" "F 1 \xA5\xF7\xB5\xF9\xA5U\xB3\xE6\xA1" "C" //�z�˵��F 1 �����U��C
      "\x1B[24;1H\x1B[1;34;44m \xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e" // �e�e�e�e�e�e�e�e�e�e�e�e�e�e�e
      "\x1B[1;37;44m \xBD\xD0\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2 " // �Ы����N���~��
      "\x1B[1;34;44m\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e  \x1B[m\r\x1B[K" //�e�e�e�e�e�e�e�e�e�e�e�e�e�e�e
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
