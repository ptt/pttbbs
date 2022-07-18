#include "user_test.h"

TEST_F(UserTest, UInfo) {
  char buf[8192] = {};
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test11] after initcuser\n");

  sprintf(buf, " ");
  put_vin((unsigned char *)buf, strlen(buf));

  now = 1655913600; //2022-06-22 16:00:00 UTC
  login_start_time = now;
  now = 1655913601; //2022-06-22 16:00:01 UTC

  user_login();

  refresh();
  reset_oflush_buf();

  sprintf(buf, "0\r\n  ");
  put_vin((unsigned char *)buf, strlen(buf));

  int ret = u_info();

  refresh();

  EXPECT_EQ(ret, 0);

  unsigned char expected[] = ""

      "\x1B[3;1H        "
      "\x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s" //�r�s�r�s�r�s
      "\x1B[m  \x1B[1;30;45m    \xA8\xCF \xA5\xCE \xAA\xCC \xB8\xEA \xAE\xC6             " //    �� �� �� �� ��
      "\x1B[m  \x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m\n\r" //�r�s�r�s�r�s
      "        \xA5N\xB8\xB9\xBC\xCA\xBA\xD9: SYSOP2 ()\n\r" //�N���ʺ�: SYSOP2 ()
      "        \xAFu\xB9\xEA\xA9m\xA6W:  \n\r" //�u��m�W:
      "        \xC2\xBE\xB7~\xBE\xC7\xBE\xFA: \n\r" //¾�~�Ǿ�:
      "        \xA9~\xA6\xED\xA6" "a\xA7}: \n\r" //�~��a�}:
      "        \xC1p\xB5\xB8\xABH\xBD" "c: \n\r" //�p���H�c:
      "           Ptt\xB9\xF4: 0 Ptt\xB9\xF4\n\r" //Ptt��: 0 Ptt��
      "        \xACO\xA7_\xA6\xA8\xA6~: \xA4w\xBA\xA1" "18\xB7\xB3\n\r" //�O�_���~: �w��18��
      "        \xB5\xF9\xA5U\xA4\xE9\xB4\xC1: 12/15/2020 21:53:29 Tue (\xA4w\xBA\xA1 564 \xA4\xD1)\n\r" //���U���: 12/15/2020 21:53:29 Tue (�w�� 564 ��)
      "        \xA8p\xA4H\xABH\xBD" "c: 0 \xAB\xCA  (\xC1\xCA\xB6R\xABH\xBD" "c: 0 \xAB\xCA)\n\r" //�p�H�H�c: 0 ��  (�ʶR�H�c: 0 ��)
      "        \xA8\xCF\xA5\xCE\xB0O\xBF\xFD: \xB5n\xA4J\xA6\xB8\xBC\xC6 26 \xA6\xB8 / \xA4\xE5\xB3\xB9 0 \xBDg\n\r" //�ϥΰO��: �n�J���� 26 �� / �峹 0 �g
      "        \xB0\xB1\xAF" "d\xB4\xC1\xB6\xA1: 269 \xA4p\xAE\xC9 35 \xA4\xC0\n\r" //���d����: 269 �p�� 35 ��
      "        \xB0h\xA4\xE5\xBC\xC6\xA5\xD8: 0\n\r" //�h��ƥ�: 0
      "        \x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m\n\r" //�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s
      "\xA6p\xAAG\xADn\xB4\xA3\xAA@\xC5v\xAD\xAD\xA1" "A\xBD\xD0\xB0\xD1\xA6\xD2\xA5\xBB\xAF\xB8\xA4\xBD\xA7G\xC4\xE6\xBF\xEC\xB2z\xB5\xF9\xA5U" //�p�G�n���@�v���A�аѦҥ������G���z���U
      "\x1B[21;1H\x1B[K\x1B[24;1H\xBD\xD0\xBF\xEF\xBE\xDC (1)\xAD\xD7\xA7\xEF\xB8\xEA\xAE\xC6 (2)\xB3]\xA9w\xB1K\xBDX (M)\xC1p\xB5\xB8\xABH\xBD" "c (V)\xBB{\xC3\xD2\xB8\xEA\xAE\xC6 [0]\xB5\xB2\xA7\xF4 " //�п�� (1)�ק��� (2)�]�w�K�X (M)�p���H�c (V)�{�Ҹ�� [0]����
      "\x1B[0;7m   \x1B[m\x1B[24;64H\r\xBD\xD0\xBF\xEF\xBE\xDC (1)\xAD\xD7\xA7\xEF\xB8\xEA\xAE\xC6 (2)\xB3]\xA9w\xB1K\xBDX (M)\xC1p\xB5\xB8\xABH\xBD" "c (V)\xBB{\xC3\xD2\xB8\xEA\xAE\xC6 [0]\xB5\xB2\xA7\xF4 "
      "\x1B[0;7m0  \x1B[m\x1B[24;65H\r"
      "\0";

  int n_remove_array = 8;
  int to_remove_array[] = {323, 324, 325, 438, 439, 440, 447, 448};

  for(int i = 0; i < n_remove_array; i++) {
    expected[to_remove_array[i]] = OFLUSH_BUF[to_remove_array[i]];
  }

  fprintf(stderr, "[user_test11] expected: %s\n", expected);

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
