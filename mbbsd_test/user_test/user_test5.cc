#include "user_test.h"

TEST_F(UserTest, UserDisplay) {
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test5] after initcuser\n");

  refresh();
  reset_oflush_buf();

  userec_t user = {};
  int uid = passwd_load_user("SYSOP10\0", &user);
  EXPECT_EQ(uid, 26);
  user_display(&user, 0);

  unsigned char expected[] = ""
      "        \x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m  " //�r�s�r�s�r�s
      "\x1B[1;30;45m    \xA8\xCF \xA5\xCE \xAA\xCC \xB8\xEA \xAE\xC6             " //    �� �� �� �� ��
      "\x1B[m  \x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m\n\r" //�r�s�r�s�r�s
      "        \xA5N\xB8\xB9\xBC\xCA\xBA\xD9: SYSOP10 ()\n\r" //        �N���ʺ�: SYSOP10 ()
      "        \xAFu\xB9\xEA\xA9m\xA6W:  \n\r" //        �u��m�W:
      "        \xC2\xBE\xB7~\xBE\xC7\xBE\xFA: \n\r" //        ¾�~�Ǿ�:
      "        \xA9~\xA6\xED\xA6" "a\xA7}: \n\r" //        �~��a�}:
      "        \xC1p\xB5\xB8\xABH\xBD" "c: devptt12346@gmail.com\n\r" //        �p���H�c: devptt12346@gmail.com
      "           Ptt\xB9\xF4: 0 Ptt\xB9\xF4\n\r" //           Ptt��: 0 Ptt��
      "        \xACO\xA7_\xA6\xA8\xA6~: \xA4w\xBA\xA1" "18\xB7\xB3\n\r" //        �O�_���~: �w��18��
      "        \xB5\xF9\xA5U\xA4\xE9\xB4\xC1: 01/25/2021 04:17:24 Mon (\xA4w\xBA\xA1 -18652 \xA4\xD1)\n\r" //        ���U���: 01/25/2021 04:17:24 Mon (�w�� -18652 ��)
      "        \xA8p\xA4H\xABH\xBD" "c: 0 \xAB\xCA  (\xC1\xCA\xB6R\xABH\xBD" "c: 0 \xAB\xCA)\n\r" //        �p�H�H�c: 0 ��  (�ʶR�H�c: 0 ��)
      "        \xA8\xCF\xA5\xCE\xB0O\xBF\xFD: \xB5n\xA4J\xA6\xB8\xBC\xC6 19 \xA6\xB8 / \xA4\xE5\xB3\xB9 0 \xBDg\n\r" //        �ϥΰO��: �n�J���� 19 �� / �峹 0 �g
      "        \xB0\xB1\xAF" "d\xB4\xC1\xB6\xA1: 0 \xA4p\xAE\xC9  0 \xA4\xC0\n\r" //        ���d����: 0 �p��  0 ��
      "        \xB0h\xA4\xE5\xBC\xC6\xA5\xD8: 0\n\r" //        �h��ƥ�: 0
      "        \x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m\n\r" //�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s
      "\xA6p\xAAG\xADn\xB4\xA3\xAA@\xC5v\xAD\xAD\xA1" "A\xBD\xD0\xB0\xD1\xA6\xD2\xA5\xBB\xAF\xB8\xA4\xBD\xA7G\xC4\xE6\xBF\xEC\xB2z\xB5\xF9\xA5U" //�p�G�n���@�v���A�аѦҥ������G���z���U
      "\0";

  refresh();

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
