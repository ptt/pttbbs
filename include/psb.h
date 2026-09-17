// Page & Service Browser
//
// A generic framework for displaying pre-generated data by a simplified
// page-view user interface to support various services.
//
// Author: Hung-Te Lin (piaip)
// --------------------------------------------------------------------------
// Copyright (c) 2010 Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved.
// Distributed under BSD license (GPL compatible).
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
// --------------------------------------------------------------------------

#ifndef _PSB_H_
#define _PSB_H_

///////////////////////////////////////////////////////////////////////////
// Constant
#define PSB_OK          (0)
#define PSB_NA          (-1)

#define PSB_MAX_CMD_LAYERS (8)
#define PSB_MAX_COLS       (8)

enum {
    CMD_PRIO_NONE = 0,   // Do not display in footer (alias or help-only)
    CMD_PRIO_NAV  = 10,  // Basic navigation (up/down/pgup/pgdn/jump)
    CMD_PRIO_LOW  = 30,  // Less frequent or admin actions
    CMD_PRIO_NORM = 50,  // Regular actions that should not change the states
    CMD_PRIO_HIGH = 80,  // Actions that may make state changes
    CMD_PRIO_TOP  = 90,  // Complicated tasks that needs attention
    CMD_PRIO_MAX  = 100, // Key actions that must be displayed
};

///////////////////////////////////////////////////////////////////////////
// Data Structure
struct cmd_layer;

typedef struct cmd_ctx {
    int key;
    int curr;
    int base;
    int total;
    int rows;
    int header_lines;
    int visible_rows;
    void (*on_select)(struct cmd_ctx *ctx, int new_curr);
    int redraw_header_lines;
    int redraw_footer_lines;
    bool redraw;
    bool reload;
    bool quit;
    bool redispatch;
    void *priv;
    const struct cmd_layer *active_layers;
    const char *caption;
} cmd_ctx_t;

typedef int (*cmd_cb_t)(cmd_ctx_t *ctx);

typedef struct {
    int key;
    const char *label;
    const char *helpstr;
    cmd_cb_t func;
    int permission;
    int prio;
    bool need_item;
} cmd_t;
#define HAVE_CMD_T 1

typedef struct cmd_layer {
    const cmd_t *cmds;
    void *priv;
} cmd_layer_t;

typedef struct PSB_CTX {
    cmd_ctx_t cmd;
    int header_lines, footer_lines;
    int allow_pbs_version_message;
    int cached_base, cached_rows, cached_cols;
    int cols;
    int col_paddings;
    int col_widths[PSB_MAX_COLS];
    const char *filename;
    void *window_buf;
    size_t item_size;
    int (*loader)(struct PSB_CTX *ctx);
    int (*header)(struct PSB_CTX *ctx);
    int (*footer)(struct PSB_CTX *ctx);
    int (*renderer)(int i, struct PSB_CTX *ctx);
    int (*empty_renderer)(struct PSB_CTX *ctx);
    int (*cursor)(int y, struct PSB_CTX *ctx);
    int (*on_key)(struct PSB_CTX *ctx);
    int (*col_measurer)(int i, int col, struct PSB_CTX *ctx);
    const cmd_t *cmds;
    const cmd_layer_t *layers;
} PSB_CTX;

extern const cmd_t psb_base_cmds[];
extern const cmd_t bbs_global_cmds[];

int cmd_dispatch_layers(const cmd_layer_t *layers, cmd_ctx_t *ctx,
                        const char *caption);
void psb_sync_cache(PSB_CTX *psbctx);
int psb_file_loader(PSB_CTX *psbctx);
bool psb_check_perm(int perm);
void cmd_set_has_item(bool has_item);
int cmd_show_help_layers(const char *caption, const cmd_layer_t *layers);
void vs_cmd_bar(int row_type, const char *prompt, const cmd_layer_t *cmd_layers);
void cmd_render_footer_layers(const char *caption, const cmd_layer_t *layers);
void cmd_bar_clear_hotspots(void);
void cmd_bar_register_custom_hotspot(int y, int x_start, int x_end, int key, const cmd_t *layer_cmd);
void cmd_bar_register_newmail_hotspot(int x_start, int x_end);

int psb_main(PSB_CTX *psbctx);

#endif
