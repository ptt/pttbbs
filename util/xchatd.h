/* $Id$ */

#ifndef _XCHAT_H_
#define _XCHAT_H_

#define XCHAT_VERSION_MAJOR 3
#define XCHAT_VERSION_MINOR 0

/* ----------------------------------------------------- */
/* XCHAT response code : RFI 3-digit			 */
/* ----------------------------------------------------- */
/* Response :						 */
/*   1xx   Informative message				 */
/*   2xx   Command ok					 */
/*   3xx   Command ok so far, send the rest of it	 */
/*   4xx   Command correct, but NG for some reason	 */
/*   5xx   Command unimplemented, incorrect, or serious	 */
/*         program error occurred			 */
/* Function :						 */
/*   x0x   Connection, setup, and miscellaneous messages */
/*   x1x   Newsgroup selection				 */
/*   x2x   Article selection				 */
/*   x3x   Distribution functions			 */
/*   x4x   Posting					 */
/*   x8x   Nonstandard extensions (AUTHINFO, XGTITLE)	 */
/*   x9x   Debugging output				 */
/* Information :					 */
/*   No defined semantics				 */
/* ----------------------------------------------------- */

/* 供新版 client 使用 */

#define MSG_LOGINOK     100
#define MSG_VERSION     103
#define MSG_MESSAGE     106

#define MSG_CHATROOM    110
#define MSG_TOPIC       113
#define MSG_ROOM        116                                                                                        
#define MSG_NICK        118
#define MSG_CLRSCR      120

#define MSG_MOTDSTART   130
#define MSG_MOTD        330
#define MSG_MOTDEND     230

#define MSG_ROOMLISTSTART   133
#define MSG_ROOMLIST        333
#define MSG_ROOMLISTEND     233
#define MSG_ROOMNOTIFY      134

#define MSG_USERLISTSTART   136
#define MSG_USERLIST        336
#define MSG_USERLISTEND     236
#define MSG_USERNOTIFY      137

#define MSG_PARTYINFO       140
#define MSG_PARTYLISTSTART  340
#define MSG_PARTYLIST       240
#define MSG_PARTYLISTEND    141

#define MSG_PRIVMSG         145
#define MSG_MYPRIVMSG       146

#define ERR_LOGIN_NICKINUSE   501
#define ERR_LOGIN_NICKERROR   502
#define ERR_LOGIN_USERONLINE  503
#define ERR_LOGIN_NOSUCHUSER  504
#define ERR_LOGIN_PASSERROR   505

static int
Isspace (int ch)
{
   return (ch == ' ' || ch == '\t' || ch == 10 || ch == 13);
}

static char *
nextword (char **str)
{
   char *head, *tail;
   int ch;

   head = *str;
   for (;;) {		

      ch = *head;
      if (!ch) {
	 *str = head;
	 return head;
      }
      if (!Isspace (ch))
	 break;
      head++;
   }

   tail = head + 1;
   while((ch = *tail)) {
       if(Isspace (ch)) {
	 *tail++ = '\0';
	 break;
      }
      tail++;
   }
   *str = tail;

   return head;
}

#endif				/* _XCHAT_H_ */
