#include "user_test.h"

TEST_F(UserTest, Customize) {
  char buf[8192] = {};
  // load SYSOP2
  fprintf(stderr, "[user_test9] to initcuser\n");
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test9] after initcuser\n");

  sprintf(buf, " ");
  put_vin((unsigned char *)buf, strlen(buf));

  now = 1655913600; //2022-06-22 16:00:00 UTC
  login_start_time = now;
  now = 1655913601; //2022-06-22 16:00:01 UTC

  user_login();

  refresh();
  reset_oflush_buf();

  sprintf(buf, "abcde1234  ");
  put_vin((unsigned char *)buf, strlen(buf));

  fprintf(stderr, "[user_test9] to Customize\n");
  Customize();

  refresh();

  unsigned char expected[] = ""
      "\x1B[H\x1B[J"
      "\x1B[0;1;37;46m\xA1i\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w\xA1j                    " //�i�ӤH�Ƴ]�w�j
      "\x1B[33m\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w" //�ӤH�Ƴ]�w
      "\x1B[0;1;37;46m                                   "
      "\x1B[m\x1B[3;1H\xB1z\xA5\xD8\xAB" "e\xAA\xBA\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w: \n\r" //�z�ثe���ӤH�Ƴ]�w:
      "\x1B[32m   \xA4\xC0\xC3\xFE       \xB4y\xADz                                       \xB3]\xA9w\xAD\xC8\x1B[m\n\r" //   ����       �y�z                                       �]�w��
      "\x1B[1;36ma\x1B[m" //a
      ". ADBANNER   \xC5\xE3\xA5\xDC\xB0\xCA\xBA" "A\xAC\xDD\xAAO                               " //. ADBANNER   ��ܰʺA�ݪO
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36mb\x1B[m" //b
      ". ADBANNER   \xC5\xE3\xA5\xDC\xA8\xCF\xA5\xCE\xAA\xCC\xA4\xDF\xB1\xA1\xC2I\xBC\xBD(\xBB\xDD\xB6}\xB1\xD2\xB0\xCA\xBA" "A\xAC\xDD\xAAO)         \xA7_\n\r" //. ADBANNER   ��ܨϥΪ̤߱��I��(�ݶ}�ҰʺA�ݪO)         �_
      "\x1B[1;36mc\x1B[m" //c
      ". MAIL       \xA9\xDA\xA6\xAC\xAF\xB8\xA5~\xABH                                 \xA7_\n\r" //. MAIL       �ڦ����~�H                                 �_
      "\x1B[1;36md\x1B[m" //d
      ". BACKUP     \xB9w\xB3]\xB3\xC6\xA5\xF7\xABH\xA5\xF3\xBBP\xA8\xE4\xA5\xA6\xB0O\xBF\xFD                     \xA7_\n\r" //. BACKUP     �w�]�ƥ��H��P�䥦�O��                     �_
      "\x1B[1;36me\x1B[m" //e
      ". LOGIN      \xA5u\xA4\xB9\xB3\\\xA8\xCF\xA5\xCE\xA6w\xA5\xFE\xB3s\xBDu(ex, ssh)\xB5n\xA4J            \xA7_\n\r" //. LOGIN      �u���\�ϥΦw���s�u(ex, ssh)�n�J            �_
      "\x1B[1;36mf\x1B[m" //f
      ". MYFAV      \xB7s\xAAO\xA6\xDB\xB0\xCA\xB6i\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                         \xA7_\n\r" //. MYFAV      �s�O�۰ʶi�ڪ��̷R                         �_
      "\x1B[1;36mg\x1B[m" //g
      ". MYFAV      \xB3\xE6\xA6\xE2\xC5\xE3\xA5\xDC\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                           \xA7_\n\r" //. MYFAV      �����ܧڪ��̷R                           �_
      "\x1B[1;36mh\x1B[m" //h
      ". MODMARK    \xC1\xF4\xC2\xC3\xA4\xE5\xB3\xB9\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9(\xB1\xC0\xA4\xE5/\xAD\xD7\xA4\xE5) (~)            \xA7_\n\r" //. MODMARK    ���ä峹�ק�Ÿ�(����/�פ�) (~)            �_
      "\x1B[1;36mi\x1B[m" //i
      ". MODMARK    \xA8\xCF\xA5\xCE\xA6\xE2\xB1m\xA5N\xB4\xC0\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9 (+)                   \xA7_\n\r" //. MODMARK    �ϥΦ�m�N���ק�Ÿ� (+)                   �_
      "\x1B[1;36mj\x1B[m" //j
      ". DBCS       \xA6\xDB\xB0\xCA\xB0\xBB\xB4\xFA\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0(\xA6p\xA5\xFE\xAB\xAC\xA4\xA4\xA4\xE5)             " //. DBCS       �۰ʰ������줸�r��(�p��������)
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36mk\x1B[m" //k
      ". DBCS       \xA9\xBF\xB2\xA4\xB3s\xBDu\xB5{\xA6\xA1\xAC\xB0\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0\xB0" "e\xA5X\xAA\xBA\xAD\xAB\xBD\xC6\xAB\xF6\xC1\xE4     " //. DBCS       �����s�u�{�������줸�r���e�X�����ƫ���
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36ml\x1B[m" //l
      ". DBCS       \xB8T\xA4\xEE\xA6" "b\xC2\xF9\xA6\xEC\xA4\xB8\xA4\xA4\xA8\xCF\xA5\xCE\xA6\xE2\xBDX(\xA5h\xB0\xA3\xA4@\xA6r\xC2\xF9\xA6\xE2)       \xA7_\n\r" //'. DBCS       �T��b���줸���ϥΦ�X(�h���@�r����)       �_'
      "\x1B[1;36mm\x1B[m" //m
      ". CURSOR     \xA8\xCF\xA5\xCE\xB7s\xA6\xA1\xC2\xB2\xA4\xC6\xB4\xE5\xBC\xD0                           " //. CURSOR     �ϥηs��²�ƴ��
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "1. PAGER      \xA4\xF4\xB2y\xBC\xD2\xA6\xA1                                   \xA4@\xAF\xEB" //1. PAGER      ���y�Ҧ�                                   �@��
      "\x1B[24;1H\x1B[1;36;44m \xA1\xBB \xBD\xD0\xAB\xF6 [a-m,1-1] \xA4\xC1\xB4\xAB\xB3]\xA9w\xA1" "A\xA8\xE4\xA5\xA6\xA5\xF4\xB7N\xC1\xE4\xB5\xB2\xA7\xF4:                   " // �� �Ы� [a-m,1-1] �����]�w�A�䥦���N�䵲��:
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] " // [�����N���~��]
      "\x1B[m\x1B[H\x1B[J\x1B[0;1;37;46m\xA1i\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w\xA1j                    " //�i�ӤH�Ƴ]�w�j
      "\x1B[33m\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w" //�ӤH�Ƴ]�w
      "\x1B[0;1;37;46m                                   "
      "\x1B[m\x1B[3;1H\xB1z\xA5\xD8\xAB" "e\xAA\xBA\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w: \n\r" //�z�ثe���ӤH�Ƴ]�w
      "\x1B[32m   \xA4\xC0\xC3\xFE       \xB4y\xADz                                       \xB3]\xA9w\xAD\xC8\x1B[m\n\r" //   ����       �y�z                                       �]�w��
      "\x1B[1;36ma\x1B[m" //a
      ". ADBANNER   \xC5\xE3\xA5\xDC\xB0\xCA\xBA" "A\xAC\xDD\xAAO                               \xA7_\n\r" //. ADBANNER   ��ܰʺA�ݪO                               �_
      "\x1B[1;36mb\x1B[m" //b
      ". ADBANNER   \xC5\xE3\xA5\xDC\xA8\xCF\xA5\xCE\xAA\xCC\xA4\xDF\xB1\xA1\xC2I\xBC\xBD(\xBB\xDD\xB6}\xB1\xD2\xB0\xCA\xBA" "A\xAC\xDD\xAAO)         \xA7_\n\r" //. ADBANNER   ��ܨϥΪ̤߱��I��(�ݶ}�ҰʺA�ݪO)         �_
      "\x1B[1;36mc\x1B[m" //c
      ". MAIL       \xA9\xDA\xA6\xAC\xAF\xB8\xA5~\xABH                                 \xA7_\n\r" //. MAIL       �ڦ����~�H                                 �_
      "\x1B[1;36md\x1B[m" //d
      ". BACKUP     \xB9w\xB3]\xB3\xC6\xA5\xF7\xABH\xA5\xF3\xBBP\xA8\xE4\xA5\xA6\xB0O\xBF\xFD                     \xA7_\n\r" //. BACKUP     �w�]�ƥ��H��P�䥦�O��                     �_
      "\x1B[1;36me\x1B[m" //e
      ". LOGIN      \xA5u\xA4\xB9\xB3\\\xA8\xCF\xA5\xCE\xA6w\xA5\xFE\xB3s\xBDu(ex, ssh)\xB5n\xA4J            \xA7_\n\r" //. LOGIN      �u���\�ϥΦw���s�u(ex, ssh)�n�J            �_
      "\x1B[1;36mf\x1B[m" //f
      ". MYFAV      \xB7s\xAAO\xA6\xDB\xB0\xCA\xB6i\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                         \xA7_\n\r" //. MYFAV      �s�O�۰ʶi�ڪ��̷R                         �_
      "\x1B[1;36mg\x1B[m" //g
      ". MYFAV      \xB3\xE6\xA6\xE2\xC5\xE3\xA5\xDC\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                           \xA7_\n\r" //. MYFAV      �����ܧڪ��̷R                           �_
      "\x1B[1;36mh\x1B[m" //h
      ". MODMARK    \xC1\xF4\xC2\xC3\xA4\xE5\xB3\xB9\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9(\xB1\xC0\xA4\xE5/\xAD\xD7\xA4\xE5) (~)            \xA7_\n\r" //. MODMARK    ���ä峹�ק�Ÿ�(����/�פ�) (~)            �_
      "\x1B[1;36mi\x1B[m" //i
      ". MODMARK    \xA8\xCF\xA5\xCE\xA6\xE2\xB1m\xA5N\xB4\xC0\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9 (+)                   \xA7_\n\r" //. MODMARK    �ϥΦ�m�N���ק�Ÿ� (+)                   �_
      "\x1B[1;36mj\x1B[m" //j
      ". DBCS       \xA6\xDB\xB0\xCA\xB0\xBB\xB4\xFA\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0(\xA6p\xA5\xFE\xAB\xAC\xA4\xA4\xA4\xE5)             " //. DBCS       �۰ʰ������줸�r��(�p��������)
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36mk\x1B[m" //k
      ". DBCS       \xA9\xBF\xB2\xA4\xB3s\xBDu\xB5{\xA6\xA1\xAC\xB0\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0\xB0" "e\xA5X\xAA\xBA\xAD\xAB\xBD\xC6\xAB\xF6\xC1\xE4     " //. DBCS       �����s�u�{�������줸�r���e�X�����ƫ���
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36ml\x1B[m" //l
      ". DBCS       \xB8T\xA4\xEE\xA6" "b\xC2\xF9\xA6\xEC\xA4\xB8\xA4\xA4\xA8\xCF\xA5\xCE\xA6\xE2\xBDX(\xA5h\xB0\xA3\xA4@\xA6r\xC2\xF9\xA6\xE2)       \xA7_\n\r" //. DBCS       �T��b���줸���ϥΦ�X(�h���@�r����)       �_
      "\x1B[1;36mm\x1B[m" //m
      ". CURSOR     \xA8\xCF\xA5\xCE\xB7s\xA6\xA1\xC2\xB2\xA4\xC6\xB4\xE5\xBC\xD0                           " //. CURSOR     �ϥηs��²�ƴ��
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "1. PAGER      \xA4\xF4\xB2y\xBC\xD2\xA6\xA1                                   \xA4@\xAF\xEB" //1. PAGER      ���y�Ҧ�                                   �@��
      "\x1B[24;1H\x1B[1;36;44m \xA1\xBB \xBD\xD0\xAB\xF6 [a-m,1-1] \xA4\xC1\xB4\xAB\xB3]\xA9w\xA1" "A\xA8\xE4\xA5\xA6\xA5\xF4\xB7N\xC1\xE4\xB5\xB2\xA7\xF4:                   " // �� �Ы� [a-m,1-1] �����]�w�A�䥦���N�䵲��:
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] " // [�����N���~��]
      "\x1B[m\x1B[H\x1B[J"
      "\x1B[0;1;37;46m\xA1i\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w\xA1j                    " //�i�ӤH�Ƴ]�w�j
      "\x1B[33m\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w" //�ӤH�Ƴ]�w
      "\x1B[0;1;37;46m                                   "
      "\x1B[m\x1B[3;1H\xB1z\xA5\xD8\xAB" "e\xAA\xBA\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w: \n\r" //�z�ثe���ӤH�Ƴ]�w:
      "\x1B[32m   \xA4\xC0\xC3\xFE       \xB4y\xADz                                       \xB3]\xA9w\xAD\xC8\x1B[m\n\r" //   ����       �y�z                                       �]�w��
      "\x1B[1;36ma\x1B[m" //a
      ". ADBANNER   \xC5\xE3\xA5\xDC\xB0\xCA\xBA" "A\xAC\xDD\xAAO                               \xA7_\n\r" //. ADBANNER   ��ܰʺA�ݪO                               �_
      "\x1B[1;36mb\x1B[m" //b
      ". ADBANNER   \xC5\xE3\xA5\xDC\xA8\xCF\xA5\xCE\xAA\xCC\xA4\xDF\xB1\xA1\xC2I\xBC\xBD(\xBB\xDD\xB6}\xB1\xD2\xB0\xCA\xBA" "A\xAC\xDD\xAAO)         " //. ADBANNER   ��ܨϥΪ̤߱��I��(�ݶ}�ҰʺA�ݪO)
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36mc\x1B[m" //c
      ". MAIL       \xA9\xDA\xA6\xAC\xAF\xB8\xA5~\xABH                                 \xA7_\n\r" //. MAIL       �ڦ����~�H                                 �_
      "\x1B[1;36md\x1B[m" //d
      ". BACKUP     \xB9w\xB3]\xB3\xC6\xA5\xF7\xABH\xA5\xF3\xBBP\xA8\xE4\xA5\xA6\xB0O\xBF\xFD                     \xA7_\n\r" //. BACKUP     �w�]�ƥ��H��P�䥦�O��                     �_
      "\x1B[1;36me\x1B[m" //e
      ". LOGIN      \xA5u\xA4\xB9\xB3\\\xA8\xCF\xA5\xCE\xA6w\xA5\xFE\xB3s\xBDu(ex, ssh)\xB5n\xA4J            \xA7_\n\r" //. LOGIN      �u���\�ϥΦw���s�u(ex, ssh)�n�J            �_
      "\x1B[1;36mf\x1B[m" //f
      ". MYFAV      \xB7s\xAAO\xA6\xDB\xB0\xCA\xB6i\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                         \xA7_\n\r" //. MYFAV      �s�O�۰ʶi�ڪ��̷R                         �_
      "\x1B[1;36mg\x1B[m" //g
      ". MYFAV      \xB3\xE6\xA6\xE2\xC5\xE3\xA5\xDC\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                           \xA7_\n\r" //. MYFAV      �����ܧڪ��̷R                           �_
      "\x1B[1;36mh\x1B[m" //h
      ". MODMARK    \xC1\xF4\xC2\xC3\xA4\xE5\xB3\xB9\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9(\xB1\xC0\xA4\xE5/\xAD\xD7\xA4\xE5) (~)            \xA7_\n\r" //. MODMARK    ���ä峹�ק�Ÿ�(����/�פ�) (~)            �_
      "\x1B[1;36mi\x1B[m" //i
      ". MODMARK    \xA8\xCF\xA5\xCE\xA6\xE2\xB1m\xA5N\xB4\xC0\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9 (+)                   \xA7_\n\r" //. MODMARK    �ϥΦ�m�N���ק�Ÿ� (+)                   �_
      "\x1B[1;36mj\x1B[m" //j
      ". DBCS       \xA6\xDB\xB0\xCA\xB0\xBB\xB4\xFA\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0(\xA6p\xA5\xFE\xAB\xAC\xA4\xA4\xA4\xE5)             " //. DBCS       �۰ʰ������줸�r��(�p��������)
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36mk\x1B[m" //k
      ". DBCS       \xA9\xBF\xB2\xA4\xB3s\xBDu\xB5{\xA6\xA1\xAC\xB0\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0\xB0" "e\xA5X\xAA\xBA\xAD\xAB\xBD\xC6\xAB\xF6\xC1\xE4     " //. DBCS       �����s�u�{�������줸�r���e�X�����ƫ���
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36ml\x1B[m" //l
      ". DBCS       \xB8T\xA4\xEE\xA6" "b\xC2\xF9\xA6\xEC\xA4\xB8\xA4\xA4\xA8\xCF\xA5\xCE\xA6\xE2\xBDX(\xA5h\xB0\xA3\xA4@\xA6r\xC2\xF9\xA6\xE2)       \xA7_\n\r" //. DBCS       �T��b���줸���ϥΦ�X(�h���@�r����)       �_
      "\x1B[1;36mm\x1B[m" //m
      ". CURSOR     \xA8\xCF\xA5\xCE\xB7s\xA6\xA1\xC2\xB2\xA4\xC6\xB4\xE5\xBC\xD0                           " //. CURSOR     �ϥηs��²�ƴ��
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "1. PAGER      \xA4\xF4\xB2y\xBC\xD2\xA6\xA1                                   \xA4@\xAF\xEB" //1. PAGER      ���y�Ҧ�                                   �@��
      "\x1B[24;1H\x1B[1;36;44m \xA1\xBB \xBD\xD0\xAB\xF6 [a-m,1-1] \xA4\xC1\xB4\xAB\xB3]\xA9w\xA1" "A\xA8\xE4\xA5\xA6\xA5\xF4\xB7N\xC1\xE4\xB5\xB2\xA7\xF4:                   " // �� �Ы� [a-m,1-1] �����]�w�A�䥦���N�䵲��:
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] " // [�����N���~��]
      "\x1B[m\x1B[H\x1B[J\x1B[0;1;37;46m\xA1i\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w\xA1j                    " //�i�ӤH�Ƴ]�w�j
      "\x1B[33m\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w" //�ӤH�Ƴ]�w
      "\x1B[0;1;37;46m                                   "
      "\x1B[m\x1B[3;1H\xB1z\xA5\xD8\xAB" "e\xAA\xBA\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w: \n\r" //�z�ثe���ӤH�Ƴ]�w:
      "\x1B[32m   \xA4\xC0\xC3\xFE       \xB4y\xADz                                       \xB3]\xA9w\xAD\xC8\x1B[m\n\r" //   ����       �y�z                                       �]�w��
      "\x1B[1;36ma\x1B[m" //a
      ". ADBANNER   \xC5\xE3\xA5\xDC\xB0\xCA\xBA" "A\xAC\xDD\xAAO                               \xA7_\n\r" //. ADBANNER   ��ܰʺA�ݪO                               �_
      "\x1B[1;36mb\x1B[m" //b
      ". ADBANNER   \xC5\xE3\xA5\xDC\xA8\xCF\xA5\xCE\xAA\xCC\xA4\xDF\xB1\xA1\xC2I\xBC\xBD(\xBB\xDD\xB6}\xB1\xD2\xB0\xCA\xBA" "A\xAC\xDD\xAAO)         " //. ADBANNER   ��ܨϥΪ̤߱��I��(�ݶ}�ҰʺA�ݪO)
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36mc\x1B[m" //c
      ". MAIL       \xA9\xDA\xA6\xAC\xAF\xB8\xA5~\xABH                                 " //. MAIL       �ڦ����~�H
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36md\x1B[m" //d
      ". BACKUP     \xB9w\xB3]\xB3\xC6\xA5\xF7\xABH\xA5\xF3\xBBP\xA8\xE4\xA5\xA6\xB0O\xBF\xFD                     \xA7_\n\r" //. BACKUP     �w�]�ƥ��H��P�䥦�O��                     �_
      "\x1B[1;36me\x1B[m" //e
      ". LOGIN      \xA5u\xA4\xB9\xB3\\\xA8\xCF\xA5\xCE\xA6w\xA5\xFE\xB3s\xBDu(ex, ssh)\xB5n\xA4J            \xA7_\n\r" //. LOGIN      �u���\�ϥΦw���s�u(ex, ssh)�n�J            �_
      "\x1B[1;36mf\x1B[m" //f
      ". MYFAV      \xB7s\xAAO\xA6\xDB\xB0\xCA\xB6i\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                         \xA7_\n\r" //. MYFAV      �s�O�۰ʶi�ڪ��̷R                         �_
      "\x1B[1;36mg\x1B[m" //g
      ". MYFAV      \xB3\xE6\xA6\xE2\xC5\xE3\xA5\xDC\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                           \xA7_\n\r" //. MYFAV      �����ܧڪ��̷R                           �_
      "\x1B[1;36mh\x1B[m" //h
      ". MODMARK    \xC1\xF4\xC2\xC3\xA4\xE5\xB3\xB9\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9(\xB1\xC0\xA4\xE5/\xAD\xD7\xA4\xE5) (~)            \xA7_\n\r" //. MODMARK    ���ä峹�ק�Ÿ�(����/�פ�) (~)            �_
      "\x1B[1;36mi\x1B[m" //i
      ". MODMARK    \xA8\xCF\xA5\xCE\xA6\xE2\xB1m\xA5N\xB4\xC0\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9 (+)                   \xA7_\n\r" //. MODMARK    �ϥΦ�m�N���ק�Ÿ� (+)                   �_
      "\x1B[1;36mj\x1B[m" //j
      ". DBCS       \xA6\xDB\xB0\xCA\xB0\xBB\xB4\xFA\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0(\xA6p\xA5\xFE\xAB\xAC\xA4\xA4\xA4\xE5)             " //. DBCS       �۰ʰ������줸�r��(�p��������)
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36mk\x1B[m" //k
      ". DBCS       \xA9\xBF\xB2\xA4\xB3s\xBDu\xB5{\xA6\xA1\xAC\xB0\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0\xB0" "e\xA5X\xAA\xBA\xAD\xAB\xBD\xC6\xAB\xF6\xC1\xE4     " //. DBCS       �����s�u�{�������줸�r���e�X�����ƫ���
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36ml\x1B[m" //l
      ". DBCS       \xB8T\xA4\xEE\xA6" "b\xC2\xF9\xA6\xEC\xA4\xB8\xA4\xA4\xA8\xCF\xA5\xCE\xA6\xE2\xBDX(\xA5h\xB0\xA3\xA4@\xA6r\xC2\xF9\xA6\xE2)       \xA7_\n\r" //. DBCS       �T��b���줸���ϥΦ�X(�h���@�r����)       �_
      "\x1B[1;36mm\x1B[m" //m
      ". CURSOR     \xA8\xCF\xA5\xCE\xB7s\xA6\xA1\xC2\xB2\xA4\xC6\xB4\xE5\xBC\xD0                           " //. CURSOR     �ϥηs��²�ƴ��
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "1. PAGER      \xA4\xF4\xB2y\xBC\xD2\xA6\xA1                                   \xA4@\xAF\xEB" //1. PAGER      ���y�Ҧ�                                   �@��
      "\x1B[24;1H\x1B[1;36;44m \xA1\xBB \xBD\xD0\xAB\xF6 [a-m,1-1] \xA4\xC1\xB4\xAB\xB3]\xA9w\xA1" "A\xA8\xE4\xA5\xA6\xA5\xF4\xB7N\xC1\xE4\xB5\xB2\xA7\xF4:                   " // �� �Ы� [a-m,1-1] �����]�w�A�䥦���N�䵲��:
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] " // [�����N���~��]
      "\x1B[m\x1B[H\x1B[J\x1B[0;1;37;46m\xA1i\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w\xA1j                    " //�i�ӤH�Ƴ]�w�j
      "\x1B[33m\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w" //�ӤH�Ƴ]�w
      "\x1B[0;1;37;46m                                   "
      "\x1B[m\x1B[3;1H\xB1z\xA5\xD8\xAB" "e\xAA\xBA\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w: \n\r" //�z�ثe���ӤH�Ƴ]�w:
      "\x1B[32m   \xA4\xC0\xC3\xFE       \xB4y\xADz                                       \xB3]\xA9w\xAD\xC8\x1B[m\n\r" //   ����       �y�z                                       �]�w��
      "\x1B[1;36ma\x1B[m" //a
      ". ADBANNER   \xC5\xE3\xA5\xDC\xB0\xCA\xBA" "A\xAC\xDD\xAAO                               \xA7_\n\r" //. ADBANNER   ��ܰʺA�ݪO                               �_
      "\x1B[1;36mb\x1B[m"
      ". ADBANNER   \xC5\xE3\xA5\xDC\xA8\xCF\xA5\xCE\xAA\xCC\xA4\xDF\xB1\xA1\xC2I\xBC\xBD(\xBB\xDD\xB6}\xB1\xD2\xB0\xCA\xBA" "A\xAC\xDD\xAAO)         " //. ADBANNER   ��ܨϥΪ̤߱��I��(�ݶ}�ҰʺA�ݪO)
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36mc\x1B[m" //c
      ". MAIL       \xA9\xDA\xA6\xAC\xAF\xB8\xA5~\xABH                                 " //. MAIL       �ڦ����~�H
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36md\x1B[m" //d
      ". BACKUP     \xB9w\xB3]\xB3\xC6\xA5\xF7\xABH\xA5\xF3\xBBP\xA8\xE4\xA5\xA6\xB0O\xBF\xFD                     " //. BACKUP     �w�]�ƥ��H��P�䥦�O��
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36me\x1B[m" //e
      ". LOGIN      \xA5u\xA4\xB9\xB3\\\xA8\xCF\xA5\xCE\xA6w\xA5\xFE\xB3s\xBDu(ex, ssh)\xB5n\xA4J            \xA7_\n\r" //. LOGIN      �u���\�ϥΦw���s�u(ex, ssh)�n�J            �_
      "\x1B[1;36mf\x1B[m" //f
      ". MYFAV      \xB7s\xAAO\xA6\xDB\xB0\xCA\xB6i\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                         \xA7_\n\r" //. MYFAV      �s�O�۰ʶi�ڪ��̷R                         �_
      "\x1B[1;36mg\x1B[m" //g
      ". MYFAV      \xB3\xE6\xA6\xE2\xC5\xE3\xA5\xDC\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                           \xA7_\n\r" //. MYFAV      �����ܧڪ��̷R                           �_
      "\x1B[1;36mh\x1B[m" //h
      ". MODMARK    \xC1\xF4\xC2\xC3\xA4\xE5\xB3\xB9\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9(\xB1\xC0\xA4\xE5/\xAD\xD7\xA4\xE5) (~)            \xA7_\n\r" //. MODMARK    ���ä峹�ק�Ÿ�(����/�פ�) (~)            �_
      "\x1B[1;36mi\x1B[m" //i
      ". MODMARK    \xA8\xCF\xA5\xCE\xA6\xE2\xB1m\xA5N\xB4\xC0\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9 (+)                   \xA7_\n\r" //. MODMARK    �ϥΦ�m�N���ק�Ÿ� (+)                   �_
      "\x1B[1;36mj\x1B[m" //j
      ". DBCS       \xA6\xDB\xB0\xCA\xB0\xBB\xB4\xFA\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0(\xA6p\xA5\xFE\xAB\xAC\xA4\xA4\xA4\xE5)             " //. DBCS       �۰ʰ������줸�r��(�p��������)
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36mk\x1B[m" //k
      ". DBCS       \xA9\xBF\xB2\xA4\xB3s\xBDu\xB5{\xA6\xA1\xAC\xB0\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0\xB0" "e\xA5X\xAA\xBA\xAD\xAB\xBD\xC6\xAB\xF6\xC1\xE4     " //. DBCS       �����s�u�{�������줸�r���e�X�����ƫ���
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36ml\x1B[m" //l
      ". DBCS       \xB8T\xA4\xEE\xA6" "b\xC2\xF9\xA6\xEC\xA4\xB8\xA4\xA4\xA8\xCF\xA5\xCE\xA6\xE2\xBDX(\xA5h\xB0\xA3\xA4@\xA6r\xC2\xF9\xA6\xE2)       \xA7_\n\r" //. DBCS       �T��b���줸���ϥΦ�X(�h���@�r����)       �_
      "\x1B[1;36mm\x1B[m" //m
      ". CURSOR     \xA8\xCF\xA5\xCE\xB7s\xA6\xA1\xC2\xB2\xA4\xC6\xB4\xE5\xBC\xD0                           " //. CURSOR     �ϥηs��²�ƴ��
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "1. PAGER      \xA4\xF4\xB2y\xBC\xD2\xA6\xA1                                   \xA4@\xAF\xEB" //1. PAGER      ���y�Ҧ�                                   �@��
      "\x1B[24;1H\x1B[1;36;44m \xA1\xBB \xBD\xD0\xAB\xF6 [a-m,1-1] \xA4\xC1\xB4\xAB\xB3]\xA9w\xA1" "A\xA8\xE4\xA5\xA6\xA5\xF4\xB7N\xC1\xE4\xB5\xB2\xA7\xF4:                   " // �� �Ы� [a-m,1-1] �����]�w�A�䥦���N�䵲��:
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] " // [�����N���~��]
      "\x1B[m\r\x1B[1;36;44m \xA1\xBB \xB1z\xA5\xB2\xB6\xB7\xA8\xCF\xA5\xCE\xA6w\xA5\xFE\xB3s\xBDu\xA4~\xAF\xE0\xAD\xD7\xA7\xEF\xA6\xB9\xB3]\xA9w                           " // �� �z�����ϥΦw���s�u�~��ק惡�]�w
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] " // [�����N���~��]
      "\x1B[m\x1B[H\x1B[J\x1B[0;1;37;46m\xA1i\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w\xA1j                    " //�i�ӤH�Ƴ]�w�j
      "\x1B[33m\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w" //�ӤH�Ƴ]�w
      "\x1B[0;1;37;46m                                   "
      "\x1B[m\x1B[3;1H\xB1z\xA5\xD8\xAB" "e\xAA\xBA\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w: \n\r" //�z�ثe���ӤH�Ƴ]�w:
      "\x1B[32m   \xA4\xC0\xC3\xFE       \xB4y\xADz                                       \xB3]\xA9w\xAD\xC8\x1B[m\n\r" //   ����       �y�z                                       �]�w��
      "\x1B[1;36ma\x1B[m" //a
      ". ADBANNER   \xC5\xE3\xA5\xDC\xB0\xCA\xBA" "A\xAC\xDD\xAAO                               \xA7_\n\r" //. ADBANNER   ��ܰʺA�ݪO                               �_
      "\x1B[1;36mb\x1B[m" //b
      ". ADBANNER   \xC5\xE3\xA5\xDC\xA8\xCF\xA5\xCE\xAA\xCC\xA4\xDF\xB1\xA1\xC2I\xBC\xBD(\xBB\xDD\xB6}\xB1\xD2\xB0\xCA\xBA" "A\xAC\xDD\xAAO)         " //. ADBANNER   ��ܨϥΪ̤߱��I��(�ݶ}�ҰʺA�ݪO)
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36mc\x1B[m" //c
      ". MAIL       \xA9\xDA\xA6\xAC\xAF\xB8\xA5~\xABH                                 " //. MAIL       �ڦ����~�H
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36md\x1B[m" //d
      ". BACKUP     \xB9w\xB3]\xB3\xC6\xA5\xF7\xABH\xA5\xF3\xBBP\xA8\xE4\xA5\xA6\xB0O\xBF\xFD                     " //. BACKUP     �w�]�ƥ��H��P�䥦�O��
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36me\x1B[m" //e
      ". LOGIN      \xA5u\xA4\xB9\xB3\\\xA8\xCF\xA5\xCE\xA6w\xA5\xFE\xB3s\xBDu(ex, ssh)\xB5n\xA4J            \xA7_\n\r" //. LOGIN      �u���\�ϥΦw���s�u(ex, ssh)�n�J            �_
      "\x1B[1;36mf\x1B[m" //f
      ". MYFAV      \xB7s\xAAO\xA6\xDB\xB0\xCA\xB6i\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                         \xA7_\n\r" //. MYFAV      �s�O�۰ʶi�ڪ��̷R                         �_
      "\x1B[1;36mg\x1B[m" //g
      ". MYFAV      \xB3\xE6\xA6\xE2\xC5\xE3\xA5\xDC\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                           \xA7_\n\r" //. MYFAV      �����ܧڪ��̷R                           �_
      "\x1B[1;36mh\x1B[m" //h
      ". MODMARK    \xC1\xF4\xC2\xC3\xA4\xE5\xB3\xB9\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9(\xB1\xC0\xA4\xE5/\xAD\xD7\xA4\xE5) (~)            \xA7_\n\r" //. MODMARK    ���ä峹�ק�Ÿ�(����/�פ�) (~)            �_
      "\x1B[1;36mi\x1B[m" //i
      ". MODMARK    \xA8\xCF\xA5\xCE\xA6\xE2\xB1m\xA5N\xB4\xC0\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9 (+)                   \xA7_\n\r" //. MODMARK    �ϥΦ�m�N���ק�Ÿ� (+)                   �_
      "\x1B[1;36mj\x1B[m" //j
      ". DBCS       \xA6\xDB\xB0\xCA\xB0\xBB\xB4\xFA\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0(\xA6p\xA5\xFE\xAB\xAC\xA4\xA4\xA4\xE5)             " //. DBCS       �۰ʰ������줸�r��(�p��������)
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36mk\x1B[m" //k
      ". DBCS       \xA9\xBF\xB2\xA4\xB3s\xBDu\xB5{\xA6\xA1\xAC\xB0\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0\xB0" "e\xA5X\xAA\xBA\xAD\xAB\xBD\xC6\xAB\xF6\xC1\xE4     " //. DBCS       �����s�u�{�������줸�r���e�X�����ƫ���
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "\x1B[1;36ml\x1B[m" //l
      ". DBCS       \xB8T\xA4\xEE\xA6" "b\xC2\xF9\xA6\xEC\xA4\xB8\xA4\xA4\xA8\xCF\xA5\xCE\xA6\xE2\xBDX(\xA5h\xB0\xA3\xA4@\xA6r\xC2\xF9\xA6\xE2)       \xA7_\n\r" //. DBCS       �T��b���줸���ϥΦ�X(�h���@�r����)       �_
      "\x1B[1;36mm\x1B[m" //m
      ". CURSOR     \xA8\xCF\xA5\xCE\xB7s\xA6\xA1\xC2\xB2\xA4\xC6\xB4\xE5\xBC\xD0                           " //. CURSOR     �ϥηs��²�ƴ��
      "\x1B[1;36m\xACO\x1B[m\n\r" //�O
      "1. PAGER      \xA4\xF4\xB2y\xBC\xD2\xA6\xA1                                   \xA4@\xAF\xEB" //1. PAGER      ���y�Ҧ�                                   �@��
      "\x1B[24;1H\x1B[1;36;44m \xA1\xBB \xBD\xD0\xAB\xF6 [a-m,1-1] \xA4\xC1\xB4\xAB\xB3]\xA9w\xA1" "A\xA8\xE4\xA5\xA6\xA5\xF4\xB7N\xC1\xE4\xB5\xB2\xA7\xF4:                   " // �� �Ы� [a-m,1-1] �����]�w�A�䥦���N�䵲��:
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] " // [�����N���~��]
      "\x1B[m\x1B[H\x1B[J\x1B[0;1;37;46m\xA1i\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w\xA1j                    " //�i�ӤH�Ƴ]�w�j
      "\x1B[33m\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w" //�ӤH�Ƴ]�w
      "\x1B[0;1;37;46m                                   "
      "\x1B[m\x1B[3;1H\x1B[1;30;40m\xB1z\xA5\xD8\xAB" "e\xAA\xBA\xAD\xD3\xA4H\xA4\xC6\xB3]\xA9w: \x1B[m\x1B[K\n\r" //�z�ثe���ӤH�Ƴ]�w:
      "\x1B[1;30;40m   \xA4\xC0\xC3\xFE       \xB4y\xADz                                       \xB3]\xA9w\xAD\xC8\x1B[m\x1B[K\n\r" //   ����       �y�z                                       �]�w��
      "\x1B[1;30;40ma. ADBANNER   \xC5\xE3\xA5\xDC\xB0\xCA\xBA" "A\xAC\xDD\xAAO                               \xA7_\x1B[m\x1B[K\n\r" //a. ADBANNER   ��ܰʺA�ݪO                               �_
      "\x1B[1;30;40mb. ADBANNER   \xC5\xE3\xA5\xDC\xA8\xCF\xA5\xCE\xAA\xCC\xA4\xDF\xB1\xA1\xC2I\xBC\xBD(\xBB\xDD\xB6}\xB1\xD2\xB0\xCA\xBA" "A\xAC\xDD\xAAO)         \xACO\x1B[m\x1B[K\n\r" //b. ADBANNER   ��ܨϥΪ̤߱��I��(�ݶ}�ҰʺA�ݪO)         �O
      "\x1B[1;30;40mc. MAIL       \xA9\xDA\xA6\xAC\xAF\xB8\xA5~\xABH                                 \xACO" //c. MAIL       �ڦ����~�H                                 �O
      "\x1B[m\x1B[K\n\r"
      "\x1B[1;30;40md. BACKUP     \xB9w\xB3]\xB3\xC6\xA5\xF7\xABH\xA5\xF3\xBBP\xA8\xE4\xA5\xA6\xB0O\xBF\xFD                     \xACO\x1B[m\x1B[K\n\r" //d. BACKUP     �w�]�ƥ��H��P�䥦�O��                     �O
      "\x1B[1;30;40me. LOGIN      \xA5u\xA4\xB9\xB3\\\xA8\xCF\xA5\xCE\xA6w\xA5\xFE\xB3s\xBDu(ex, ssh)\xB5n\xA4J            \xA7_\x1B[m\x1B[K\n\r" //e. LOGIN      �u���\�ϥΦw���s�u(ex, ssh)�n�J            �_
      "\x1B[1;30;40mf. MYFAV      \xB7s\xAAO\xA6\xDB\xB0\xCA\xB6i\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                         \xA7_\x1B[m\x1B[K\n\r" //f. MYFAV      �s�O�۰ʶi�ڪ��̷R                         �_
      "\x1B[1;30;40mg. MYFAV      \xB3\xE6\xA6\xE2\xC5\xE3\xA5\xDC\xA7\xDA\xAA\xBA\xB3\xCC\xB7R                           \xA7_\x1B[m\x1B[K\n\r" //g. MYFAV      �����ܧڪ��̷R                           �_
      "\x1B[1;30;40mh. MODMARK    \xC1\xF4\xC2\xC3\xA4\xE5\xB3\xB9\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9(\xB1\xC0\xA4\xE5/\xAD\xD7\xA4\xE5) (~)            \xA7_\x1B[m\x1B[K\n\r" //h. MODMARK    ���ä峹�ק�Ÿ�(����/�פ�) (~)            �_
      "\x1B[1;30;40mi. MODMARK    \xA8\xCF\xA5\xCE\xA6\xE2\xB1m\xA5N\xB4\xC0\xAD\xD7\xA7\xEF\xB2\xC5\xB8\xB9 (+)                   \xA7_\x1B[m\x1B[K\n\r" //i. MODMARK    �ϥΦ�m�N���ק�Ÿ� (+)                   �_
      "\x1B[1;30;40mj. DBCS       \xA6\xDB\xB0\xCA\xB0\xBB\xB4\xFA\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0(\xA6p\xA5\xFE\xAB\xAC\xA4\xA4\xA4\xE5)             \xACO\x1B[m\x1B[K\n\r" //j. DBCS       �۰ʰ������줸�r��(�p��������)             �O
      "\x1B[1;30;40mk. DBCS       \xA9\xBF\xB2\xA4\xB3s\xBDu\xB5{\xA6\xA1\xAC\xB0\xC2\xF9\xA6\xEC\xA4\xB8\xA6r\xB6\xB0\xB0" "e\xA5X\xAA\xBA\xAD\xAB\xBD\xC6\xAB\xF6\xC1\xE4     \xACO\x1B[m\x1B[K\n\r" //k. DBCS       �����s�u�{�������줸�r���e�X�����ƫ���     �O
      "\x1B[1;30;40ml. DBCS       \xB8T\xA4\xEE\xA6" "b\xC2\xF9\xA6\xEC\xA4\xB8\xA4\xA4\xA8\xCF\xA5\xCE\xA6\xE2\xBDX(\xA5h\xB0\xA3\xA4@\xA6r\xC2\xF9\xA6\xE2)       \xA7_\x1B[m\x1B[K\n\r" //l. DBCS       �T��b���줸���ϥΦ�X(�h���@�r����)       �_
      "\x1B[1;30;40mm. CURSOR     \xA8\xCF\xA5\xCE\xB7s\xA6\xA1\xC2\xB2\xA4\xC6\xB4\xE5\xBC\xD0                           \xACO\x1B[m\x1B[K\n\r" //m. CURSOR     �ϥηs��²�ƴ��                           �O
      "\x1B[1;30;40m1. PAGER      \xA4\xF4\xB2y\xBC\xD2\xA6\xA1                                   \xA4@\xAF\xEB" //1. PAGER      ���y�Ҧ�                                   �@��
      "\x1B[m\x1B[K\x1B[23;1H\xB3]\xA9w\xA4w\xC0x\xA6s\xA1" "C\n\r" //�]�w�w�x�s�C
      "\x1B[1;36;44m \xA1\xBB \xB3]\xA9w\xA7\xB9\xA6\xA8                                                   " // �� �]�w����
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] \x1B[m\r\x1B[K" // [�����N���~��]
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
