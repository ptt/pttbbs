#include "admin_test.h"

TEST_F(AdminTest, Setperms) {
  char buf[TEST_BUFFER_SIZE] = {};

  bzero(buf, TEST_BUFFER_SIZE);
  sprintf(buf, "ABC12\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  unsigned int ret = setperms(0, str_permboard);
  unsigned int expected_ret = 1 + 2 + 4 + 134217728 + 268435456; //2^0+2^1+2^2+2^27+2^28
  EXPECT_EQ(ret, expected_ret);

  refresh();
  log_oflush_buf();

  unsigned char expected[] = "\x1B\x5B\x35\x3B\x31\x48"
      "\x41\x2E\x20\x28\xB5\x4C\xA7\x40\xA5\xCE\x29\x20\x20\x20" //A. (無作用)
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\xA2\xE6\x20\x20\x20\x20\x20\x20\x20\x20" //Ｘ
      "\x20\x20\x20\x20\x20\x20\x51\x2E\x20" //      Q.
      "\xA4\xA3\xA5\x69\xBC\x4E\x20\x20\x20\x20\x20" //不可噓
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\xA2\xE6\x0A\x0D" //          Ｘ
      "\x42\x2E\x20\xA4\xA3\xA6\x43\xA4\x4A\xB2\xCE\xAD\x70\x20\x20\x20" //B. 不列入統計
      "\x20\x20\x20\x20\x20\x20\x20\x20\xA2\xE6" //        Ｘ
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20\x52\x2E\x20\x28\xB5\x4C" //    R. (無
      "\xA7\x40\xA5\xCE\x29\x20\x20\x20\x20\x20" //作用)
      "\x20\x20\x20\x20\x20\x20\x20\x20\xA2\xE6\x0A\x0D" //        Ｘ
      "\x43\x2E\x20\x28\xB5\x4C\xA7\x40\xA5\xCE\x29\x20\x20\x20\x20\x20\x20\x20" //C. (無作用)
      "\x20\x20\x20\x20\x20\x20\xA2\xE6\x20\x20" //      Ｘ
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x53\x2E\x20\xAD\xAD\xAC\xDD" //  S. 限看
      "\xAA\x4F\xB7\x7C\xAD\xFB\xB5\x6F\xA4\xE5\x20" //板會員發文
      "\x20\x20\x20\x20\x20\x20\xA2\xE6\x0A\x0D" //      Ｘ
      "\x44\x2E\x20\xB8\x73\xB2\xD5\xAA\x4F\x20" //D. 群組板
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20\xA2\xE6\x20\x20\x20\x20" //    Ｘ
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x54\x2E\x20\x47\x75\x65\x73\x74\xA5\x69" //T. Guest可
      "\xA5\x48\xB5\x6F\xAA\xED\x20\x20\x20\x20" //以發表
      "\x20\x20\x20\x20\xA2\xE6\x0A\x0D" //    Ｘ
      "\x45\x2E\x20\xC1\xF4\xC2\xC3\xAA\x4F\x20\x20\x20" //E. 隱藏板
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\xA2\xE6\x20\x20\x20\x20\x20\x20" //  Ｘ
      "\x20\x20\x20\x20\x20\x20\x20\x20\x55\x2E" //        U.
      "\x20\xA7\x4E\xC0\x52\x20\x20\x20\x20\x20" // 冷靜
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\xA2\xE6\x0A\x0D" //  Ｘ
      "\x46\x2E\x20\xAD\xAD\xA8\xEE\x28\xA4\xA3\xBB\xDD\xB3\x5D" //F. 限制(不需設
      "\xA9\x77\x29\x20\x20\x20\x20\x20\x20\x20" //定)
      "\xA2\xE6\x20\x20\x20\x20\x20\x20\x20\x20" //Ｘ
      "\x20\x20\x20\x20\x20\x20\x56\x2E\x20" //      V.
      "\xA6\xDB\xB0\xCA\xAF\x64\xC2\xE0\xBF\xFD" //自動留轉錄
      "\xB0\x4F\xBF\xFD\x20\x20\x20\x20\x20\x20\x20" //記錄
      "\xA2\xE6\x0A\x0D"//Ｘ
      "\x47\x2E\x20\xB0\xCE\xA6\x57\xAA\x4F\x20\x20\x20\x20\x20\x20\x20" //G. 匿名板
      "\x20\x20\x20\x20\x20\x20\x20\x20\xA2\xE6" //        Ｘ
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20\x57\x2E\x20\xB8\x54" //    W. 禁
      "\xA4\xEE\xA7\xD6\xB3\x74\xB1\xC0\xA4\xE5\x20" //止快速推文
      "\x20\x20\x20\x20\x20\x20\x20\x20\xA2\xE6\x0A\x0D" //        Ｘ
      "\x48\x2E\x20\xB9\x77\xB3\x5D\xB0\xCE\xA6\x57\xAA\x4F\x20\x20\x20\x20\x20" //H. 預設匿名板
      "\x20\x20\x20\x20\x20\x20\xA2\xE6\x20\x20" //      Ｘ
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x58\x2E\x20\xB1\xC0\xA4\xE5" //  X. 推文
      "\xB0\x4F\xBF\xFD\x20\x49\x50\x20\x20\x20\x20" //記錄 IP
      "\x20\x20\x20\x20\x20\x20\xA2\xE6\x0A\x0D" //      Ｘ
      "\x49\x2E\x20\xB5\x6F\xA4\xE5\xB5\x4C" //I. 發文無
      "\xBC\xFA\xC0\x79\x20\x20\x20\x20\x20\x20\x20" //獎勵
      "\x20\x20\x20\x20\xA2\xE6\x20\x20\x20\x20" //    Ｘ
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x59\x2E\x20\xA4\x51\xA4\x4B\xB8\x54\x20" //Y. 十八禁
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20\xA2\xE6\x0A\x0D" //    Ｘ
      "\x4A\x2E\x20\xB3\x73\xB8\x70\xB1\x4D\xA5\xCE\xAC\xDD\xAA\x4F" //J. 連署專用看板
      "\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\xA2\xE6\x20\x20\x20\x20\x20\x20" //  Ｘ
      "\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x5A\x2E\x20\xB9\xEF\xBB\xF4\xA6\xA1\xB1\xC0\xA4\xE5" //Z. 對齊式推文
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\xA2\xE6\x0A\x0D" //           Ｘ
      "\x4B\x2E\x20\xA4\x77\xC4\xB5\xA7\x69\xAD\x6E\xBC\x6F\xB0\xA3" //K. 已警告要廢除
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\xA2\xE6\x20\x20\x20\x20\x20\x20\x20\x20" //Ｘ
      "\x20\x20\x20\x20\x20\x20"
      "\x30\x2E\x20\xA4\xA3\xA5\x69\xA6\xDB\xA7\x52\x20\x20\x20" //0. 不可自刪
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\xA2\xE6\x0A\x0D" //Ｘ
      "\x4C\x2E\x20\xBC\xF6\xAA\xF9\xAC\xDD\xAA\x4F\xB8\x73\xB2\xD5\x20" //L. 熱門看板群組
      "\x20\x20\x20\x20\x20\x20\x20\x20\xA2\xE6" //        Ｘ
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20\x31\x2E\x20\xAA\x4F\xA5\x44\xA5\x69\xA7\x52\xAF\x53\xA9\x77\xA4\xE5\xA6\x72" //    1. 板主可刪特定文字
      "\x20\x20\x20\x20\x20\xA2\xE6\x0A\x0D"
      "\x4D\x2E\x20\xA4\xA3\xA5\x69\xB1\xC0\xC2\xCB" //M. 不可推薦
      "\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20\x20\x20\xA2\xE6\x20\x20" //      Ｘ
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x32\x2E\x20\xA8\x53\xB7\x51\xA8\xEC\x20" //  2. 沒想到
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\xA2\xE6\x0A\x0D" //Ｘ
      "\x4E\x2E\x20\xA4\x70\xA4\xD1\xA8\xCF\xA5\x69\xB0\xCE\xA6\x57\x20" //N. 小天使可匿名
      "\x20\x20\x20\x20\x20\x20\x20\x20\xA2\xE6\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x33\x2E\x20\xA8\x53\xB7\x51\xA8\xEC\x20" //3. 沒想到
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20\xA2\xE6\x0A\x0D" //    Ｘ
      "\x4F\x2E\x20\xAA\x4F\xA5\x44\xB3\x5D\xA9\x77\xA6\x43\xA4\x4A\xB0\x4F\xBF\xFD\x20\x20\x20" //O. 板主設定列入記錄
      "\x20\x20"
      "\xA2\xE6\x20\x20\x20\x20\x20\x20" //Ｘ
"\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x34\x2E\x20\xA8\x53\xB7\x51\xA8\xEC\x20\x20\x20" //4. 沒想到
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\xA2\xE6\x0A\x0D" //  Ｘ
      "\x50\x2E\x20\xB3\x73\xB5\xB2\xAC\xDD\xAA\x4F" //P. 連結看板
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\xA2\xE6\x20\x20\x20\x20\x20\x20\x20\x20" //Ｘ
      "\x20\x20\x20\x20\x20\x20"
      "\x35\x2E\x20\xA8\x53\xB7\x51\xA8\xEC\x20\x20\x20\x20\x20" //5. 沒想到
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\xA2\xE6\x0A\x0D" //Ｘ
      "\x1B\x5B\x4B\x1B\x5B\x32\x34\x3B\x31\x48\x1B\x5B\x31\x3B\x33\x36\x3B\x34\x34\x6D" //\x1b[H\x1b[24;18H\x1b[1;36;44m
      "\x20\xA1\xBB\x20\xBD\xD0\xAB\xF6\x20\x5B\x41\x2D\x35\x5D\x20" // ◆ 請按 [A-5]
      "\xA4\xC1\xB4\xAB\xB3\x5D\xA9\x77\xA1\x41" //切換設定，
      "\xAB\xF6\x20\x5B\x52\x65\x74\x75\x72\x6E\x5D" //按 [Return]
      "\x20\xB5\xB2\xA7\xF4\xA1\x47\x20\x20\x20" // 結束：
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20"
      "\x1B\x5B\x31\x3B\x33\x33\x3B\x34\x36\x6D" //\x1b[1;33;46m
      "\x20\x5B\xAB\xF6\xA5\xF4\xB7\x4E\xC1\xE4\xC4\x7E\xC4\xF2\x5D\x20" // [按任意鍵繼續]
      "\x1B\x5B\x6D\x1B\x5B\x4B\x1B\x5B\x35\x3B\x32\x35\x48" //\x1b[m\x1bH\x1b[5;25H
      "\xA3\xBE" //ˇ
      "\x1B\x5B\x32\x34\x3B\x31\x48\x1B\x5B\x31\x3B\x33\x36\x3B\x34\x34\x6D" //\x1b[24;19H\x1b[1;36;44m
      "\x20\xA1\xBB\x20\xBD\xD0\xAB\xF6\x20\x5B\x41\x2D\x35\x5D\x20" // ◆ 請按 [A-5]
      "\xA4\xC1\xB4\xAB\xB3\x5D\xA9\x77\xA1\x41" //切換設定，
      "\xAB\xF6\x20\x5B\x52\x65\x74\x75\x72\x6E\x5D\x20" //按 [Return]
      "\xB5\xB2\xA7\xF4\xA1\x47\x20\x20" //結束：
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x1B"
      "\x5B\x31\x3B\x33\x33\x3B\x34\x36\x6D" //\x1b[1;33;46m
      "\x20\x5B\xAB\xF6\xA5\xF4\xB7\x4E\xC1\xE4\xC4\x7E\xC4\xF2\x5D\x20" // [按任意鍵繼續]
      "\x1B\x5B\x6D\x1B\x5B\x36\x3B\x32\x35\x48" //\x1b[m\x16;25H
      "\xA3\xBE" //ˇ
      "\x1B\x5B\x32\x34\x3B\x31\x48\x1B\x5B\x31\x3B\x33\x36\x3B\x34\x34\x6D" //\x1b[24;1H\x1b[1;36;44m
      "\x20\xA1\xBB\x20\xBD\xD0\xAB\xF6\x20\x5B\x41\x2D\x35\x5D\x20\xA4\xC1" // ◆ 請按 [A-5] 切
      "\xB4\xAB\xB3\x5D\xA9\x77\xA1\x41\xAB\xF6" //換設定，按
      "\x20\x5B\x52\x65\x74\x75\x72\x6E\x5D\x20" // [Return]
      "\xB5\xB2\xA7\xF4\xA1\x47\x20\x20\x20\x20" //結束：
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20\x20\x20"
      "\x1B\x5B\x31\x3B\x33\x33\x3B\x34\x36\x6D" //\x1b[1;33;46m
      "\x20\x5B\xAB\xF6\xA5\xF4\xB7\x4E\xC1\xE4\xC4\x7E\xC4\xF2\x5D\x20" // [按任意鍵繼續]
      "\x1B\x5B\x6D\x1B\x5B\x37\x3B\x32\x35\x48" //\x1b[m\x1b[7;25H
      "\xA3\xBE" //ˇ
      "\x1B\x5B\x32\x34\x3B\x31\x48\x1B\x5B\x31\x3B\x33\x36\x3B\x34\x34\x6D" //\x1b[24;1H\x1b[1;36;44m
      "\x20\xA1\xBB\x20\xBD\xD0\xAB\xF6\x20\x5B\x41\x2D\x35\x5D\x20\xA4\xC1\xB4\xAB" // ◆ 請按 [A-5] 切換
      "\xB3\x5D\xA9\x77\xA1\x41\xAB\xF6\x20" //設定，按
      "\x5B\x52\x65\x74\x75\x72\x6E\x5D\x20\xB5\xB2\xA7\xF4\xA1\x47" //[Return] 結束：
      "\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20"
      "\x1B\x5B\x31\x3B\x33\x33\x3B\x34\x36\x6D" //\x1b[1;33;46m
      "\x20\x5B\xAB\xF6\xA5\xF4\xB7\x4E\xC1\xE4\xC4\x7E\xC4\xF2\x5D\x20" // [按任意鍵繼續]
      "\x1B\x5B\x6D\x1B\x5B\x31\x36\x3B\x36\x35\x48" //\x1b[m\x1b[16;65H
      "\xA3\xBE" //ˇ
      "\x1B\x5B\x32\x34\x3B\x31\x48\x1B\x5B\x31\x3B\x33\x36\x3B\x34\x34\x6D" //\x1b[24;1H\x1b[1;36;44m
      "\x20\xA1\xBB\x20\xBD\xD0\xAB\xF6\x20\x5B" // ◆ 請按 [
      "\x41\x2D\x35\x5D\x20\xA4\xC1\xB4\xAB" //A-5] 切換
      "\xB3\x5D\xA9\x77\xA1\x41\xAB\xF6\x20\x5B\x52" //設定，按 [R
      "\x65\x74\x75\x72\x6E\x5D\x20\xB5\xB2\xA7\xF4\xA1\x47" //eturn] 結束：
      "\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20"
      "\x1B\x5B\x31\x3B\x33\x33\x3B\x34\x36\x6D" //\x1b[1;33;46m
      "\x20\x5B\xAB\xF6\xA5\xF4\xB7\x4E\xC1\xE4\xC4\x7E\xC4\xF2\x5D\x20" // [按任意鍵繼續]
      "\x1B\x5B\x6D\x1B\x5B\x31\x37\x3B\x36\x35\x48" //\x1b[m\x1b[17;65H
      "\xA3\xBE" //ˇ
      "\x1B\x5B\x32\x34\x3B\x31\x48\x1B\x5B\x31\x3B\x33\x36\x3B\x34\x34\x6D"
      "\x20\xA1\xBB\x20\xBD\xD0\xAB\xF6\x20\x5B\x41" // ◆ 請按 [A
      "\x2D\x35\x5D\x20\xA4\xC1\xB4\xAB\xB3\x5D" //-5] 切換設
      "\xA9\x77\xA1\x41\xAB\xF6\x20\x5B\x52\x65" //定，按 [Re
      "\x74\x75\x72\x6E\x5D\x20\xB5\xB2\xA7\xF4\xA1\x47" //turn] 結束：
      "\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
      "\x20\x20"
      "\x1B\x5B\x31\x3B\x33\x33\x3B\x34\x36\x6D" //\x1b[1;33;46m
      "\x20\x5B\xAB\xF6\xA5\xF4\xB7\x4E\xC1\xE4\xC4\x7E\xC4\xF2\x5D\x20" // [按任意鍵繼續]
      "\x1B\x5B\x6D\x0D\x1B\x5B\x4B" //\x1b[m\x0d\x1b[H
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
