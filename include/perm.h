/* $Id$ */
#ifndef INCLUDE_PERM_H
#define INCLUDE_PERM_H

#define PERM_BASIC        000000000001 /* 基本權力       */
#define PERM_CHAT         000000000002 /* 進入聊天室     */
#define PERM_PAGE         000000000004 /* 找人聊天       */
#define PERM_POST         000000000010 /* 發表文章       */
#define PERM_LOGINOK      000000000020 /* 註冊程序認證   */
#define PERM_MAILLIMIT    000000000040 /* 信件無上限     */
#define PERM_CLOAK        000000000100 /* 目前隱形中     */
#define PERM_SEECLOAK     000000000200 /* 看見忍者       */
#define PERM_XEMPT        000000000400 /* 永久保留帳號   */
#define PERM_SYSOPHIDE    000000001000 /* 站長隱身術     */
#define PERM_BM           000000002000 /* 板主           */
#define PERM_ACCOUNTS     000000004000 /* 帳號總管       */
#define PERM_CHATROOM     000000010000 /* 聊天室總管     */
#define PERM_BOARD        000000020000 /* 看板總管       */
#define PERM_SYSOP        000000040000 /* 站長           */
#define PERM_BBSADM       000000100000 /* BBSADM         */
#define PERM_NOTOP        000000200000 /* 不列入排行榜   */
#define PERM_VIOLATELAW   000000400000 /* 違法通緝中     */
#ifdef PLAY_ANGEL
#define PERM_ANGEL        000001000000 /* 有資格擔任小天使 */
#endif
#define OLD_PERM_NOOUTMAIL    000001000000 /* 不接受站外的信 */
#define PERM_NOREGCODE    000002000000 /*不允許認證碼註冊*/
#define PERM_VIEWSYSOP    000004000000 /* 視覺站長       */
#define PERM_LOGUSER      000010000000 /* 觀察使用者行蹤 */
#define PERM_NOCITIZEN    000020000000 /* 搋奪公權       */
#define PERM_SYSSUPERSUBOP    000040000000 /* 群組長         */
#define PERM_ACCTREG      000100000000 /* 帳號審核組     */
#define PERM_PRG          000200000000 /* 程式組         */
#define PERM_ACTION       000400000000 /* 活動組         */
#define PERM_PAINT        001000000000 /* 美工組         */
#define PERM_POLICE_MAN   002000000000 /* 警察總管       */
#define PERM_SYSSUBOP     004000000000 /* 小組長         */
#define PERM_OLDSYSOP     010000000000 /* 退休站長       */
#define PERM_POLICE       020000000000 /* 警察 */

#define NUMPERMS        32

#define PERM_DEFAULT    (PERM_BASIC | PERM_CHAT | PERM_PAGE )
#define PERM_MANAGER    (PERM_ACCTREG | PERM_ACTION | PERM_PAINT)
#define PERM_ADMIN      (PERM_ACCOUNTS | PERM_SYSOP | PERM_SYSSUBOP | PERM_SYSSUPERSUBOP | PERM_MANAGER | PERM_BM)
#define PERM_ALLBOARD   (PERM_SYSOP | PERM_BOARD)
#define PERM_LOGINCLOAK (PERM_SYSOP | PERM_ACCOUNTS)
#define PERM_SEEULEVELS (PERM_SYSOP)
#define PERM_SEEBLEVELS (PERM_SYSOP | PERM_BM)
#define PERM_NOTIMEOUT  (PERM_SYSOP)
#define PERM_READMAIL   (PERM_BASIC)
#define PERM_FORWARD    (PERM_BASIC)    /* to do the forwarding */
#define PERM_INTERNET   (PERM_LOGINOK)  /* 身份認證過關的才能寄信到 Internet */

#define HasUserPerm(x)  (cuser.userlevel & (x))
#define PERM_HIDE(u)    (u && (u)->userlevel & PERM_SYSOPHIDE)

#define IS_BOARD(bptr)   ((bptr)->brdname[0] && \
                          !((bptr)->brdattr & BRD_GROUPBOARD))
#define IS_GROUP(bptr)   ((bptr)->brdname[0] && \
                          ((bptr)->brdattr & BRD_GROUPBOARD))

#define IS_OPENBRD(bptr) \
   (!(((bptr)->brdattr & (BRD_HIDE | BRD_TOP)) ||             \
      ((bptr)->level && !((bptr)->brdattr & BRD_POSTMASK) &&  \
       ((bptr)->level &                                       \
        ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK)))))
#endif
