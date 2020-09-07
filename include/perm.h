/* $Id$ */
#ifndef INCLUDE_PERM_H
#define INCLUDE_PERM_H

#define MENU_UNREGONLY (-1)	    // �S���: �b menu �̪�ܥ����U����
				    // ���S�Dguest���b���~�ݱo��

#define PERM_BASIC        000000000001 /* ���v�O       */
#define PERM_CHAT         000000000002 /* �i�J��ѫ�     */
#define PERM_PAGE         000000000004 /* ��H���       */
#define PERM_POST         000000000010 /* �o��峹       */
#define PERM_LOGINOK      000000000020 /* ���U�{�ǻ{��   */
#define PERM_MAILLIMIT    000000000040 /* �H��L�W��     */
#define PERM_CLOAK        000000000100 /* �ثe���Τ�     */
#define PERM_SEECLOAK     000000000200 /* �ݨ��Ԫ�       */
#define PERM_XEMPT        000000000400 /* �ä[�O�d�b��   */
#define PERM_SYSOPHIDE    000000001000 /* ���������N     */
#define PERM_BM           000000002000 /* �O�D           */
#define PERM_ACCOUNTS     000000004000 /* �b���`��       */
#define PERM_CHATROOM     000000010000 /* ��ѫ��`��     */
#define PERM_BOARD        000000020000 /* �ݪO�`��       */
#define PERM_SYSOP        000000040000 /* ����           */
#define PERM_BBSADM       000000100000 /* BBSADM         */
#define PERM_NOTOP        000000200000 /* ���C�J�Ʀ�]   */
#define PERM_VIOLATELAW   000000400000 /* �H�k�q�r��     */
#define PERM_ANGEL        000001000000 /* ��������p�Ѩ� */
#define PERM_NOREGCODE    000002000000 /* �����\�{�ҽX���U */
#define PERM_VIEWSYSOP    000004000000 /* ��ı����       */
#define PERM_LOGUSER      000010000000 /* �[��ϥΪ̦��� */
#define PERM_NOCITIZEN    000020000000 /* ݶ�ܤ��v       */
#define PERM_SYSSUPERSUBOP 000040000000 /* �s�ժ�         */
#define PERM_ACCTREG      000100000000 /* �b���f�ֲ�     */
#define PERM_PRG          000200000000 /* �{����         */
#define PERM_ACTION       000400000000 /* ���ʲ�         */
#define PERM_PAINT        001000000000 /* ���u��         */
#define PERM_POLICE_MAN   002000000000 /* ĵ���`��       */
#define PERM_SYSSUBOP     004000000000 /* �p�ժ�         */
#define PERM_OLDSYSOP     010000000000 /* �h�𯸪�       */
#define PERM_POLICE       020000000000 /* ĵ�� */
// 32 �Ӥw�g�����Υ��F�C �᭱�S���F�C

#define NUMPERMS        32

#define PERM_DEFAULT    (PERM_BASIC | PERM_CHAT | PERM_PAGE )
#define PERM_MANAGER    (PERM_ACCTREG | PERM_ACTION | PERM_PAINT)
#define PERM_ADMIN      (PERM_ACCOUNTS | PERM_BOARD | PERM_SYSOP | \
                         PERM_SYSSUBOP | PERM_SYSSUPERSUBOP | PERM_MANAGER)
#define PERM_LOGINCLOAK (PERM_SYSOP | PERM_ACCOUNTS)
#define PERM_SEEULEVELS (PERM_SYSOP)
#define PERM_SEEBLEVELS (PERM_SYSOP | PERM_BM)
#define PERM_NOTIMEOUT  (PERM_SYSOP)
#define PERM_READMAIL   (PERM_BASIC)
#define PERM_FORWARD    (PERM_LOGINOK)    /* to do the forwarding */
#define PERM_INTERNET   (PERM_LOGINOK)  /* �����{�ҹL�����~��H�H�� Internet */

#define HasUserPerm(x)  ((cuser.userlevel & (x)) != 0)
#define HasBasicUserPerm(x)  (HasUserPerm(PERM_BASIC) && HasUserPerm(x))
#define PERM_HIDE(u)    (u && (u)->userlevel & PERM_SYSOPHIDE)

#define ROLE_ANGEL_CIA          0x00000001  /* Angel: CIA. */
#define ROLE_ANGEL_ACTIVITY     0x00000002  /* Angel: For activities, no assign. */
#define ROLE_ANGEL_ARCHANGEL    0x00000080  /* Angel: Arch-Angel */
#define ROLE_POLICE_ANONYMOUS   0x00000100  /* Police: Anonymous account. */

#define ROLE_HIDE_FROM          (ROLE_ANGEL_CIA | ROLE_POLICE_ANONYMOUS)

#define HasUserRole(x)  ((cuser.role & (x)) != 0)

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
