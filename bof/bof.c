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
    [BOF_ERR_COULDNT_CREATE_POST] = "couldnt_create_post",
    [BOF_ERR_COULDNT_CREATE_COMMENT] = "couldnt_create_comment",
    [BOF_ERR_BBS_READ_ONLY] = "bbs_read_only",
    [BOF_ERR_COULDNT_UPDATE_COMMENT] = "couldnt_update_comment",
    [BOF_ERR_BBS_STORAGE_NOT_SUPPORTED] = "bbs_storage_not_supported",
    [BOF_ERR_NO_COMMENT_EDIT_ALLOWED] = "no_comment_edit_allowed",
    [BOF_ERR_COULDNT_CREATE_REPORT] = "couldnt_create_report",
    [BOF_ERR_BBS_ALREADY_REPORTED] = "bbs_already_reported",
    [BOF_ERR_COULDNT_RESOLVE_REPORT] = "couldnt_resolve_report",
    [BOF_ERR_RATE_LIMITED] = "rate_limit_error",
    [BOF_ERR_COULDNT_FIND_PERSON] = "couldnt_find_person",
    [BOF_ERR_BANNED_FROM_COMMUNITY] = "banned_from_community",
    [BOF_ERR_BBS_NOT_ALLOWED] = "bbs_not_allowed",
};

const char *
bof_error_name(bof_error_t err)
{
    if (err < 0 || err >= BOF_ERR_MAX || !bof_error_names[err])
        return "unknown";
    return bof_error_names[err];
}
