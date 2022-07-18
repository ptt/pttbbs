#include "user_test.h"

TEST_F(UserTest, MailViolatelaw) {
  char buf[8192] = {};
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test6] after initcuser\n");

  refresh();
  reset_oflush_buf();

  mail_violatelaw("SYSOP10\0", "SYSOP2\0", "test-reason\0", "test-result\0");

  char homedir[8192] = {};
  sethomedir(homedir, "SYSOP10\0");

  fileheader_t fhdr = {};
  int n_records = get_num_records(homedir, sizeof(fhdr));
  fprintf(stderr, "[user_test6] n_records: %d\n", n_records);
  int ret = get_record(homedir, &fhdr, sizeof(fhdr), n_records);
  EXPECT_EQ(ret, 1);

  char homepath[8192] = {};
  sethomepath(homepath, "SYSOP10\0");
  char the_filename[8192] = {};
  sprintf(the_filename, "%s/%s", homepath, fhdr.filename);

  fprintf(stderr, "[user_test6] the_filename: %s\n", the_filename);

  EXPECT_EQ(dashf(the_filename), 1);

  int sz = dashs(the_filename);
  int fd = open(the_filename, O_RDONLY);
  bzero(buf, sizeof(buf));
  read(fd, buf, sz);

  close(fd);

  unsigned char expected[] = ""
      "\xA7@\xAA\xCC: [Ptt\xC4\xB5\xB9\xEE\xA7\xBD]\n" //�@��: [Pttĵ�]
      "\xBC\xD0\xC3" "D: [\xB3\xF8\xA7i] \xB9H\xAAk\xB3\xF8\xA7i\n" //���D: [���i] �H�k���i
      "\xAE\xC9\xB6\xA1: Thu Jan  1 00:00:00 1970\n" //�ɶ�: Thu Jan  1 00:00:00 1970
      "\n"
      "\x1B[1;32mSYSOP2"
      "\x1B[m\xA7P\xA8M\xA1G\n" //�P�M�G
      "     \x1B[1;32mSYSOP10"
      "\x1B[m\xA6]"
      "\x1B[1;35mtest-reason"
      "\x1B[m\xA6\xE6\xAC\xB0\xA1" "A\n" //�欰�A
      "\xB9H\xA4\xCF\xA5\xBB\xAF\xB8\xAF\xB8\xB3W\xA1" "A\xB3" "B\xA5H" //�H�ϥ������W�A�B�H
      "\x1B[1;35mtest-result"
      "\x1B[m\xA1" "A\xAFS\xA6\xB9\xB3q\xAA\xBE\n" //�A�S���q��
      "\n"
      "\xBD\xD0\xA8\xEC PttLaw \xAC" "d\xB8\xDF\xAC\xDB\xC3\xF6\xAAk\xB3W\xB8\xEA\xB0T\xA1" "A\xA8\xC3\xB1q\xA5" "D\xBF\xEF\xB3\xE6\xB6i\xA4J:\n" //�Ш� PttLaw �d�߬����k�W��T�A�ñq�D���i�J:
      "(P)lay\xA1i\xAET\xBC\xD6\xBBP\xA5\xF0\xB6\xA2\xA1j=>(P)ay\xA1i\xA2\xDEtt\xB6q\xB3" "c\xA9\xB1 \xA1j=> (1)ViolateLaw \xC3\xBA\xBB@\xB3\xE6\n" //(P)lay�i�T�ֻP�𶢡j=>(P)ay�i��tt�q�c�� �j=> (1)ViolateLaw ú�@��
      "\xA5H\xC3\xBA\xA5\xE6\xBB@\xB3\xE6\xA1" "C\n" //�Hú��@��C
      "\0";

  EXPECT_STREQ(buf, (char *)expected);
}
