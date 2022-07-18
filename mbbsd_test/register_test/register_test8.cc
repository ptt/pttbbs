#include "register_test.h"

TEST_F(RegisterTest, CheckRegister) {
  char buf[8192] = {};

  // load SYSOP2
  sprintf(buf, " y\r\nTEST8@TEST.EDU\r\ny\r\nv6jJGyTsompza\r\ny\r\ntestnewreg8\r\ntestpass\r\ntestpass\r\ntestnick8\r\ntestreal8\r\ntestcareer8\r\ntestaddr12345\xA5\xAB" "8\r\ny\r\nn\r\n");
  put_vin((unsigned char *)buf, strlen(buf));
  new_register();

  fprintf(stderr, "[register_test8] after newregister\n");

  refresh();
  reset_oflush_buf();

  check_register();

  refresh();

  unsigned char expected[] = ""
      "\x1B[H\x1B[J"
      "\x1B[1;37;46m \xA5\xBC\xA7\xB9\xA6\xA8\xB5\xF9\xA5U\xBB{\xC3\xD2 " // ���������U�{��
      "\x1B[1;37;45m \xB1z\xAA\xBA\xB1" "b\xB8\xB9\xA9|\xA5\xBC\xA7\xB9\xA6\xA8\xBB{\xC3\xD2                                          " // �z���b���|�������{��
      "\x1B[m\x1B[10;1H  \xB1z\xA5\xD8\xAB" "e\xA9|\xA5\xBC\xB3q\xB9L\xB5\xF9\xA5U\xBB{\xC3\xD2\xB5{\xA7\xC7\xA1" "A" //  �z�ثe�|���q�L���U�{�ҵ{�ǡA
      "\x1B[1;33m\xBD\xD0\xA6\xDC (U) -> (R) \xB6i\xA6\xE6\xB1" "b\xB8\xB9\xBB{\xC3\xD2\x1B[m\n\r" //�Ц� (U) -> (R) �i��b���{��
      "  \xA5H\xC0\xF2\xB1o\xB6i\xB6\xA5\xA8\xCF\xA5\xCE\xC5v\xA4O\xA1" "C" //  �H��o�i���ϥ��v�O�C
      "\x1B[13;1H  \xA6p\xAAG\xB1z\xA4\xA7\xAB" "e\xB4\xBF\xA8\xCF\xA5\xCE email \xB5\xA5\xBB{\xC3\xD2\xA4\xE8\xA6\xA1\xB3q\xB9L\xB5\xF9\xA5U\xBB{\xC3\xD2\xA6\xFD\xA4S\xAC\xDD\xA8\xEC\xA6\xB9\xB0T\xAE\xA7\xA1" "A\n\r" //  �p�G�z���e���ϥ� email ���{�Ҥ覡�q�L���U�{�Ҧ��S�ݨ즹�T���A
      "  \xA5N\xAA\xED\xB1z\xAA\xBA\xBB{\xC3\xD2\xA5\xD1\xA9\xF3\xB8\xEA\xAE\xC6\xA4\xA3\xA7\xB9\xBE\xE3\xA4w\xB3Q\xA8\xFA\xAE\xF8 (\xB1`\xA8\xA3\xA9\xF3\xA5\xD3\xBD\xD0\xB6}\xB7s\xAC\xDD\xAAO\xAA\xBA\xAAO\xA5" "D)\xA1" "C" //  �N��z���{�ҥѩ��Ƥ�����w�Q���� (�`����ӽж}�s�ݪO���O�D)�C
      "\x1B[24;1H\x1B[1;34;44m \xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e" // �e�e�e�e�e�e�e�e�e�e�e�e�e�e�e
      "\x1B[1;37;44m \xBD\xD0\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2 " // �Ы����N���~��
      "\x1B[1;34;44m\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e  \x1B[m\r\x1B[K"
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
