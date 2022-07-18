#include "user_test.h"

TEST_F(UserTest, ULoginview) {
  char buf[8192] = {};
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test3] after initcuser\n");

  EXPECT_EQ(cuser.loginview, 0);

  refresh();
  reset_oflush_buf();

  sprintf(buf, "d\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  u_loginview();

  EXPECT_EQ(cuser.loginview, 8);

  unsigned char expected[] = ""
      "\x1B[H\x1B[J"
      "\x1B[1;37;46m\xA1i \xB3]\xA9w\xB6i\xAF\xB8\xB5" "e\xAD\xB1 \xA1j" //�i �]�w�i���e�� �j
      "\x1B[m\x1B[5;1H    B. \xA4\xDF\xB1\xA1\xC2I\xBC\xBD\xB1\xC6\xA6\xE6\xBA]       \xA2\xE6              \n\r" //    B. �߱��I���Ʀ�]       ��
      "    C. \xA4Q\xA4j\xB1\xC6\xA6\xE6\xBA]           \xA2\xE6              \n\r" //    C. �Q�j�Ʀ�]           ��
      "    D. \xA6\xCA\xA4j\xB1\xC6\xA6\xE6\xBA]           \xA2\xE6              \n\r" //    D. �ʤj�Ʀ�]           ��
      "    G. \xA4\xB5\xA4\xE9\xA4Q\xA4j\xB8\xDC\xC3" "D         \xA2\xE6              \n\r" //    G. ����Q�j���D         ��
      "    H. \xA4@\xB6g\xA4\xAD\xA4Q\xA4j\xB8\xDC\xC3" "D       \xA2\xE6              \n\r" //    H. �@�g���Q�j���D       ��
      "    I. \xA4\xB5\xA4\xD1\xA4W\xAF\xB8\xA4H\xA6\xB8         \xA2\xE6              \n\r" //    I. ���ѤW���H��         ��
      "    J. \xACQ\xA4\xE9\xA4W\xAF\xB8\xA4H\xA6\xB8         \xA2\xE6              \n\r" //    J. �Q��W���H��         ��
      "    K. \xBE\xFA\xA5v\xA4W\xAA\xBA\xA4\xB5\xA4\xD1         \xA2\xE6              \n\r" //    K. ���v�W������         ��
      "    L. \xBA\xEB\xB5\xD8\xB0\xCF\xB1\xC6\xA6\xE6\xBA]         \xA2\xE6              \n\r" //    L. ��ذϱƦ�]         ��
      "    M. \xAC\xDD\xAAO\xA4H\xAE\xF0\xB1\xC6\xA6\xE6\xBA]       \xA2\xE6              " //    M. �ݪO�H��Ʀ�]       ��
      "\x1B[24;1H\x1B[1;36;44m \xA1\xBB \xBD\xD0\xAB\xF6 [A-M] \xA4\xC1\xB4\xAB\xB3]\xA9w\xA1" "A\xAB\xF6 [Return] \xB5\xB2\xA7\xF4\xA1G                    " // �� �Ы� [A-M] �����]�w�A�� [Return] �����G
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] \x1B[m" // [�����N���~��]
      "\x1B[H\x1B[J\x1B[1;37;46m\xA1i \xB3]\xA9w\xB6i\xAF\xB8\xB5" "e\xAD\xB1 \xA1j\x1B[m" //�i �]�w�i���e�� �j
      "\x1B[5;1H    B. \xA4\xDF\xB1\xA1\xC2I\xBC\xBD\xB1\xC6\xA6\xE6\xBA]       \xA2\xE6              \n\r" //    B. �߱��I���Ʀ�]       ��
      "    C. \xA4Q\xA4j\xB1\xC6\xA6\xE6\xBA]           \xA2\xE6              \n\r" //    C. �Q�j�Ʀ�]           ��
      "    D. \xA6\xCA\xA4j\xB1\xC6\xA6\xE6\xBA]           \xA3\xBE              \n\r" //    D. �ʤj�Ʀ�]           ��
      "    G. \xA4\xB5\xA4\xE9\xA4Q\xA4j\xB8\xDC\xC3" "D         \xA2\xE6              \n\r" //    G. ����Q�j���D         ��
      "    H. \xA4@\xB6g\xA4\xAD\xA4Q\xA4j\xB8\xDC\xC3" "D       \xA2\xE6              \n\r" //    H. �@�g���Q�j���D       ��
      "    I. \xA4\xB5\xA4\xD1\xA4W\xAF\xB8\xA4H\xA6\xB8         \xA2\xE6              \n\r" //    I. ���ѤW���H��         ��
      "    J. \xACQ\xA4\xE9\xA4W\xAF\xB8\xA4H\xA6\xB8         \xA2\xE6              \n\r" //    J. �Q��W���H��         ��
      "    K. \xBE\xFA\xA5v\xA4W\xAA\xBA\xA4\xB5\xA4\xD1         \xA2\xE6              \n\r" //    K. ���v�W������         ��
      "    L. \xBA\xEB\xB5\xD8\xB0\xCF\xB1\xC6\xA6\xE6\xBA]         \xA2\xE6              \n\r" //    L. ��ذϱƦ�]         ��
      "    M. \xAC\xDD\xAAO\xA4H\xAE\xF0\xB1\xC6\xA6\xE6\xBA]       \xA2\xE6              " //    M. �ݪO�H��Ʀ�]       ��
      "\x1B[24;1H\x1B[1;36;44m \xA1\xBB \xBD\xD0\xAB\xF6 [A-M] \xA4\xC1\xB4\xAB\xB3]\xA9w\xA1" "A\xAB\xF6 [Return] \xB5\xB2\xA7\xF4\xA1G                    " // �� �Ы� [A-M] �����]�w�A�� [Return] �����G
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] \x1B[m" // [�����N���~��]
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
