/* $Id$ */
#ifndef PTT_CONST_H
#define PTT_CONST_H

#ifdef __cplusplus
extern "C" {
#endif

#define LEN_COMMENT_HEADER 15
#define LEN_COMMENT_PREFIX_COLOR 8

#define LEN_COMMENT_DATETIME_IN_LINE 11

#define COMMENT_CROSS_POST_HEADER "※ " ANSI_COLOR(1;32)
#define LEN_COMMENT_CROSS_POST_HEADER 10 
#define LEN_COMMENT_CROSS_POST_PREFIX_COLOR 7
#define COMMENT_CROSS_POST_PREFIX ":轉錄至"
#define LEN_COMMENT_CROSS_POST_PREFIX 7
#define COMMENT_CROSS_POST_HIDDEN_BOARD "某隱形看板"

#define LEN_COMMENT_RESET_KARMA_INFIX 4
#define COMMENT_RESET_KARMA_INFIX " 於 "
#define LEN_COMMENT_RESET_KARMA_POSTFIX 13
#define COMMENT_RESET_KARMA_POSTFIX " 將推薦值歸零"    

#define LEN_LINE_EDIT_PREFIX 9
#define LINE_EDIT_PREFIX "※ 編輯: "

#define TITLE_PREFIX "標題:"
#define LEN_TITLE_PREFIX 5

#define TIME_PREFIX "時間:"
#define LEN_TIME_PREFIX 5
#define LEN_DAY_IN_WEEK_STRING 3
#define LEN_TIME_STRING 24

#define WEBLINK_POSTFIX "html"
#define LEN_WEBLINK_POSTFIX 4
#define LEN_AID_POSTFIX 3
#define LEN_AID_INFIX 1
#define LEN_AID_TIMESTAMP 10

#define ORIGIN_PREFIX "※ 發信站:"
#define LEN_ORIGIN_PREFIX 10

#define LOADING "正在載入文章中"

#define FOOTER_VEDIT3_PREFIX " 編輯文章 "
#define FOOTER_VEDIT3_INFIX " (^Z/F1)說明 (^P/^G)插入符號/範本 (^X/^Q)離開\t"
#define FOOTER_VEDIT3_POSTFIX "||%s|%c%c%c%c||%3d:%d:%3d" // XXX need to replace with original symbols

#define VEDIT3_PROMPT_INSERT "插入"
#define VEDIT3_PROMPT_REPLACE "取代"

#ifdef __cplusplus
}
#endif

#endif /* PTT_CONST_H */
