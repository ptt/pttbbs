#include "admin_test.h"

TEST_F(AdminTest, MNewbrd) {
  char buf[TEST_BUFFER_SIZE] = {};

  bzero(buf, TEST_BUFFER_SIZE);
  sprintf(buf, "testing\r\ntest\r\ntest-title\r\ny\r\nSYSOP\r\nn\r\naa");
  put_vin((unsigned char *)buf, strlen(buf));

  int the_class = 5;
  int recover =0;
  int ret = m_newbrd(the_class, recover);
  EXPECT_EQ(ret, 0);

  refresh();
  log_oflush_buf();

  fprintf(stderr, "[admin_test13] To set expected\n");

  unsigned char expected[] = "\x1B[H\x1B[J"
      "\x1B[1;37;46m"
      "\xA1i \xAB\xD8\xA5\xDF\xB7s\xAAO \xA1j" //�i �إ߷s�O �j
      "\x1B[m\x1B[4;1H"
      "\xBD\xD0\xBF\xE9\xA4J\xAC\xDD\xAAO\xA6W\xBA\xD9: " //�п�J�ݪO�W��:
      "\x1B[0;7m"
      "             "
      "\x1B[m\x1B[4;17H\r"
      "\xBD\xD0\xBF\xE9\xA4J\xAC\xDD\xAAO\xA6W\xBA\xD9: "
      "\x1B[0;7m"
      "t            "
      "\x1B[m\x1B[4;18H\r"
      "\xBD\xD0\xBF\xE9\xA4J\xAC\xDD\xAAO\xA6W\xBA\xD9: "
      "\x1B[0;7m"
      "te           "
      "\x1B[m\x1B[4;19H\r"
      "\xBD\xD0\xBF\xE9\xA4J\xAC\xDD\xAAO\xA6W\xBA\xD9: "
      "\x1B[0;7m"
      "tes          "
      "\x1B[m\x1B[4;20H\r"
      "\xBD\xD0\xBF\xE9\xA4J\xAC\xDD\xAAO\xA6W\xBA\xD9: "
      "\x1B[0;7m"
      "test         "
      "\x1B[m\x1B[4;21H\r"
      "\xBD\xD0\xBF\xE9\xA4J\xAC\xDD\xAAO\xA6W\xBA\xD9: "
      "\x1B[0;7m"
      "testi        "
      "\x1B[m\x1B[4;22H\r"
      "\xBD\xD0\xBF\xE9\xA4J\xAC\xDD\xAAO\xA6W\xBA\xD9: "
      "\x1B[0;7m"
      "testin       "
      "\x1B[m\x1B[4;23H\r"
      "\xBD\xD0\xBF\xE9\xA4J\xAC\xDD\xAAO\xA6W\xBA\xD9: "
      "\x1B[0;7m"
      "testing      "
      "\x1B[m\x1B[4;24H\x1B[7;1H"
      "\xAC\xDD\xAAO\xC3\xFE\xA7O\xA1G" //�ݪO���O�G
      "\x1B[0;7m"
      "     "
      "\x1B[m\x1B[7;11H\r"
      "\xAC\xDD\xAAO\xC3\xFE\xA7O\xA1G" //�ݪO���O�G
      "\x1B[0;7m"
      "t    "
      "\x1B[m\x1B[7;12H\r"
      "\xAC\xDD\xAAO\xC3\xFE\xA7O\xA1G"
      "\x1B[0;7m"
      "te   "
      "\x1B[m\x1B[7;13H\r"
      "\xAC\xDD\xAAO\xC3\xFE\xA7O\xA1G"
      "\x1B[0;7m"
      "tes  "
      "\x1B[m\x1B[7;14H\r"
      "\xAC\xDD\xAAO\xC3\xFE\xA7O\xA1G"
      "\x1B[0;7m"
      "test "
      "\x1B[m\x1B[7;15H\x1B[9;1H"
      "\xAC\xDD\xAAO\xA5" "D\xC3" "D\xA1G" //�ݪO�D�D�G
      "\x1B[0;7m"
      "                                                 "
      "\x1B[m\x1B[9;11H\r"
      "\xAC\xDD\xAAO\xA5" "D\xC3" "D\xA1G"
      "\x1B[0;7m"
      "t                                                "
      "\x1B[m\x1B[9;12H\r"
      "\xAC\xDD\xAAO\xA5" "D\xC3" "D\xA1G"
      "\x1B[0;7m"
      "te                                               "
      "\x1B[m\x1B[9;13H\r"
      "\xAC\xDD\xAAO\xA5" "D\xC3" "D\xA1G"
      "\x1B[0;7m"
      "tes                                              "
      "\x1B[m\x1B[9;14H\r"
      "\xAC\xDD\xAAO\xA5" "D\xC3" "D\xA1G"
      "\x1B[0;7m"
      "test                                             "
      "\x1B[m\x1B[9;15H\r"
      "\xAC\xDD\xAAO\xA5" "D\xC3" "D\xA1G"
      "\x1B[0;7m"
      "test-                                            "
      "\x1B[m\x1B[9;16H\r"
      "\xAC\xDD\xAAO\xA5" "D\xC3" "D\xA1G"
      "\x1B[0;7m"
      "test-t                                           "
      "\x1B[m\x1B[9;17H\r"
      "\xAC\xDD\xAAO\xA5" "D\xC3" "D\xA1G"
      "\x1B[0;7m"
      "test-ti                                          "
      "\x1B[m\x1B[9;18H\r"
      "\xAC\xDD\xAAO\xA5" "D\xC3" "D\xA1G"
      "\x1B[0;7m"
      "test-tit                                         "
      "\x1B[m\x1B[9;19H\r"
      "\xAC\xDD\xAAO\xA5" "D\xC3" "D\xA1G"
      "\x1B[0;7m"
      "test-titl                                        "
      "\x1B[m\x1B[9;20H\r"
      "\xAC\xDD\xAAO\xA5" "D\xC3" "D\xA1G"
      "\x1B[0;7m"
      "test-title                                       "
      "\x1B[m\x1B[9;21H\n\r"
      "\xACO\xAC\xDD\xAAO? (N:\xA5\xD8\xBF\xFD) (Y/n)\xA1G" //�O�ݪO? (N:�ؿ�) (Y/n)�G
      "\x1B[0;7m"
      "   "
      "\x1B[m\x1B[10;25H\r"
      "\xACO\xAC\xDD\xAAO? (N:\xA5\xD8\xBF\xFD) (Y/n)\xA1G"
      "\x1B[0;7m"
      "y  "
      "\x1B[m\x1B[10;26H\x1B[12;1H"
      "\xAAO\xA5" "D\xA6W\xB3\xE6\xA1G" //�O�D�W��G
      "\x1B[0;7m"
      "                                       "
      "\x1B[m\x1B[12;11H\r"
      "\xAAO\xA5" "D\xA6W\xB3\xE6\xA1G"
      "\x1B[0;7m"
      "S                                      "
      "\x1B[m\x1B[12;12H\r"
      "\xAAO\xA5" "D\xA6W\xB3\xE6\xA1G"
      "\x1B[0;7m"
      "SY                                     "
      "\x1B[m\x1B[12;13H\r"
      "\xAAO\xA5" "D\xA6W\xB3\xE6\xA1G"
      "\x1B[0;7m"
      "SYS                                    "
      "\x1B[m\x1B[12;14H\r"
      "\xAAO\xA5" "D\xA6W\xB3\xE6\xA1G"
      "\x1B[0;7m"
      "SYSO                                   "
      "\x1B[m\x1B[12;15H\r"
      "\xAAO\xA5" "D\xA6W\xB3\xE6\xA1G"
      "\x1B[0;7m"
      "SYSOP                                  "
      "\x1B[m\x1B[12;16H\n\r"
      "\xB3]\xA9w\xB4\xD1\xB0\xEA (0)\xB5L (1)\xA4\xAD\xA4l\xB4\xD1 (2)\xB6H\xB4\xD1 (3)\xB3\xF2\xB4\xD1" //�]�w�Ѱ� (0)�L (1)���l�� (2)�H�� (3)���
      "\x1B[0;7m"
      "0   "
      "\x1B[m\x1B[13;42H\r"
      "\xB3]\xA9w\xB4\xD1\xB0\xEA (0)\xB5L (1)\xA4\xAD\xA4l\xB4\xD1 (2)\xB6H\xB4\xD1 (3)\xB3\xF2\xB4\xD1"
      "\x1B[0;7m"
      "0n  "
      "\x1B[m\x1B[13;43H\x1B[24;1H\x1B[1;34;44m"
      " \xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e" // �e�e�e�e�e�e�e�e�e�e�e�e�e�e�e
      "\x1B[1;37;44m"
      " \xBD\xD0\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2 " // �Ы����N���~��
      "\x1B[1;34;44m"
      "\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e  " //�e�e�e�e�e�e�e�e�e�e�e�e�e�e�e
      "\x1B[m\x1B[1;1H"
      "\xB7s\xAAO\xA6\xA8\xA5\xDF" //�s�O����
      "\x1B[24;1H\x1B[1;34;44m"
      " \xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e" // �e�e�e�e�e�e�e�e�e�e�e�e�e�e�e
      "\x1B[1;37;44m"
      " \xBD\xD0\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2 " // �Ы����N���~��
      "\x1B[1;34;44m"
      "\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e  " //�e�e�e�e�e�e�e�e�e�e�e�e�e�e�e
      "\x1B[m\r\x1B[K"
      "\0";

  fprintf(stderr, "[admin_test13] To expect_streq: expected: %zu OFLUSH_BUF: %zu\n", strlen((char *)expected), strlen((char *)OFLUSH_BUF));
  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
