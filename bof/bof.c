/*
 * bof.c - Birds Of Feather: common parts.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include "bof.h"

static const char *const bof_error_names[BOF_ERR_MAX] = {
    [BOF_OK] = "ok",
    [BOF_ERR_UNKNOWN] = "unknown",
    [BOF_ERR_INVALID_BODY_FIELD] = "invalid_body_field",
    [BOF_ERR_NOT_FOUND] = "not_found",
    [BOF_ERR_COULDNT_FIND_COMMUNITY] = "couldnt_find_community",
    [BOF_ERR_COULDNT_FIND_POST] = "couldnt_find_post",
    [BOF_ERR_COULDNT_FIND_COMMENT] = "couldnt_find_comment",
    [BOF_ERR_COULDNT_UPDATE_POST] = "couldnt_update_post",
    [BOF_ERR_NOT_A_MODERATOR] = "not_a_moderator",
    [BOF_ERR_NO_POST_EDIT_ALLOWED] = "no_post_edit_allowed",
    [BOF_ERR_LOCKED] = "locked",
};

const char *
bof_error_name(bof_error_t err)
{
    if (err < 0 || err >= BOF_ERR_MAX || !bof_error_names[err])
        return "unknown";
    return bof_error_names[err];
}
