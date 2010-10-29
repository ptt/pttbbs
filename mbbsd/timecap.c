/* $Id $ */
#include "bbs.h"

// Time Capsule / Magical Index
//
// Management of edit history and deleted (archived) objects
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
//
// The Time Capsule provides an interface to "append" objects into a managed
// data pool. There's no diff, compress, incremental, or expiring because the
// time capsule is designed to be very fast and least impact to original system.
// You should do those by a post-processing cron job (ex, compress or remove
// files periodically).
//
// Currently any objects can perform two kinds of actions in Time Capsule:
//  - Revision: Record a new by with numerical (auto-inc) number.
//  - Archive:  Append blob information in an index file
//
// For BBS,
//  editing posts should perform a Revision
//  deleting posts should perform a Revision and then Archive.
//
// NOTE: Revision index is now using "add one byte for each rev", that helps us
//       to get the rev by dashs instead of reading content. However that only
//       works fine if the revisions are small. The statistics shown that most
//       revisions are less than 100 so it's working fine for a traditional BBS,
//       but not for a wiki-like system.

///////////////////////////////////////////////////////////////////////////
// Constants

enum TIME_CAPSULE_ACTION_TYPE {
    TIME_CAPSULE_ACTION_REVISION = 1,
    TIME_CAPSULE_ACTION_ARCHIVE,
};

#define TIME_CAPSULE_BASE_FOLDER_NAME   ".timecap/"
#define TIME_CAPSULE_ARCHIVE_INDEX_NAME "archive.idx"
#define TIME_CAPSULE_REVISION_INDEX_NAME ".rev"

///////////////////////////////////////////////////////////////////////////
// Core Function

static int
timecap_get_max_revision(const char *capsule_index) {
    off_t sz = dashs(capsule_index);  
    // TODO we can change implementation to read int instead of dashs
    return sz > 0 ? (int)sz : 0;
}

static int
timecap_get_max_archive(const char *archive_index, size_t ref_blob_size) {
    off_t sz = dashs(archive_index);
    if (!ref_blob_size || sz < 1)
        return 0;
    // TODO what if ref_blob_size%sz != 0 ?
    // Maybe we should keep some metadata in the archive in the future
    return sz / ref_blob_size;
}

static int
timecap_get_archive_content(const char *archive_index, int index, int nblobs,
                            void *blobsptr, size_t szblob) {
    int fd;
    off_t srcsz = dashs(archive_index),
          offset = (off_t) index * szblob,
          readsz = nblobs * szblob;

    assert(blobsptr && szblob && nblobs);
    if (offset < 0 || srcsz < offset || srcsz < offset + readsz)
        return 0;

    fd = open(archive_index, O_RDONLY);
    if (fd < 0)
        return 0;
    if (lseek(fd, offset, SEEK_SET) != offset ||
        read(fd, (char*)blobsptr, readsz) != readsz) {
        close(fd);
        return 0;
    }
    close(fd);
    return 1;
}

static int
timecap_convert_revision_filename(int rev,
                                  char *rev_index_path,
                                  size_t sz_index_path) {
    char revstr[STRLEN];
    char *s = strrchr(rev_index_path, '.');
    if (!s++)
        return 0;

    *s = 0;
    sprintf(revstr, "%03d", rev);
    strlcat(rev_index_path, revstr, sz_index_path);
    return 1;
}

static int
timecap_add_revision(const char *object_path, const char *capsule_index) {
    int rev, fd;
    char capsule_path[PATHLEN];
    const char nul = 0;

    strlcpy(capsule_path, capsule_index, sizeof(capsule_path));
    // solve index and revision
    rev = timecap_get_max_revision(capsule_index);
    if (rev++ < 0)
        rev = 1;
    assert(rev != 0);

    timecap_convert_revision_filename(rev, capsule_path, sizeof(capsule_path));
    fd = OpenCreate(capsule_index, O_WRONLY | O_EXLOCK | O_APPEND);
    if (fd < 0)
        return 0;

    // we don't use Link because time capsule are supposed to not
    // causing extra disk usage.
    if (link(object_path, capsule_path) != 0 ||
            write(fd, &nul, 1) != 1) {
        close(fd);
        return 0;
    }

    close(fd);
    return rev;
}

static int
timecap_add_archive(const char *capsule_index,
                    const void *ref_blob, size_t ref_blob_size)
{
    int fd;

    if (!ref_blob || !ref_blob_size)
        return 0;

    // check if the index file is broken or incompatible.
    // if (dashs(capsule_index) % ref_blob_size)
    //    return 0;

    // solve new store name
    fd = OpenCreate(capsule_index, O_WRONLY | O_EXLOCK | O_APPEND);
    if (fd < 0)
        return 0;

    // the blob must provide enough information to get the object name.
    if (write(fd, ref_blob, ref_blob_size) != ref_blob_size) {
        close(fd);
        return 0;
    }
    close(fd);
    return 1;
}

static int
timecap_solve_base_folder(int create, const char *object_path,
                          char *folder, size_t sz_folder) {
    // currently we only support file objects
    if (create && !dashf(object_path))
        return 0;

    assert(sz_folder >= PATHLEN); // default size in setdirpath
    setdirpath(folder, object_path, TIME_CAPSULE_BASE_FOLDER_NAME);
    if (!dashd(folder) && (!create || Mkdir(folder) != 0))
        return 0;

    return 1;
}

static int
timecap_solve_revision_index(int create, const char *object_path,
                             char *index_path, size_t sz_index_path) {
    const char *object_name = strrchr(object_path, '/');
    if (!object_name++ || !*object_name)
        return 0;

    if (!timecap_solve_base_folder(create, object_path,
                                   index_path, sz_index_path))
        return 0;

    strlcat(index_path, object_name, sz_index_path);
    strlcat(index_path, TIME_CAPSULE_REVISION_INDEX_NAME, sz_index_path);
    return 1;
}

static int
timecap_solve_archive_index(int create, const char *object_path,
                            char *index_path, size_t sz_index_path) {
    if (!timecap_solve_base_folder(create, object_path,
                                   index_path, sz_index_path))
        return 0;

    strlcat(index_path, TIME_CAPSULE_ARCHIVE_INDEX_NAME, sz_index_path);
    return 1;
}

static int
timecap_add_object(const char *object_path,
                   enum TIME_CAPSULE_ACTION_TYPE action_type,
                   const void *ref_blob,
                   size_t ref_blob_size) {
    char capsule_index[PATHLEN];

    // make sure the base folder can be created.
    switch (action_type) {
        case TIME_CAPSULE_ACTION_REVISION:
            if (!timecap_solve_revision_index(1, object_path, capsule_index,
                                              sizeof(capsule_index)))
                return 0;
            return timecap_add_revision(object_path, capsule_index);

        case TIME_CAPSULE_ACTION_ARCHIVE:
            if (!timecap_solve_archive_index(1, object_path, capsule_index,
                                             sizeof(capsule_index)))
                return 0;
            return timecap_add_archive(capsule_index, ref_blob, ref_blob_size);
    }
    assert(!"unknown time capsule reference type");
    return 0;
}

int
timecap_query_object_max_number(const char *object_path,
                                enum TIME_CAPSULE_ACTION_TYPE action_type,
                                size_t ref_blob_size) {
    char capsule_index[PATHLEN];

    switch(action_type) {
        case TIME_CAPSULE_ACTION_REVISION:
            if (!timecap_solve_revision_index(0, object_path, capsule_index,
                                              sizeof(capsule_index)))
                return 0;
            return timecap_get_max_revision(capsule_index);

        case TIME_CAPSULE_ACTION_ARCHIVE:
            if (!timecap_solve_archive_index(0, object_path, capsule_index,
                                             sizeof(capsule_index)))
                return 0;
            return timecap_get_max_archive(capsule_index, ref_blob_size);
    }
    assert(!"unknown time capsule reference type");
    return 0;
}

int
timecap_query_object_refblobs(const char *object_path,
                              int index,
                              int nblobs,
                              void *ref_blob,
                              size_t ref_blob_size) {
    char capsule_index[PATHLEN];

    if (!timecap_solve_archive_index(0, object_path, capsule_index,
                                     sizeof(capsule_index)))
        return 0;
    return timecap_get_archive_content(
            capsule_index, index, nblobs, ref_blob, ref_blob_size);
}

///////////////////////////////////////////////////////////////////////////
// Export API

int
timecapsule_add_revision(const char *filename) {
    return timecap_add_object(filename, TIME_CAPSULE_ACTION_REVISION, NULL, 0);
}

int 
timecapsule_get_max_revision_number(const char *filename) {
    return timecap_query_object_max_number(
            filename, TIME_CAPSULE_ACTION_REVISION, 0);
}

int
timecapsule_get_max_archive_number(const char *filename, size_t szrefblob) {
    return timecap_query_object_max_number(
            filename, TIME_CAPSULE_ACTION_ARCHIVE, szrefblob);
}

int 
timecapsule_archive(const char *filename, const void *ref, size_t szref) {
    return timecap_add_object(
            filename, TIME_CAPSULE_ACTION_ARCHIVE, ref, szref);
}

int
timecapsule_get_by_revision(const char *filename, int rev,
                            char *rev_path, size_t sz_rev_path) {
    if (!timecap_solve_revision_index(0, filename, rev_path, sz_rev_path))
        return 0;
    timecap_convert_revision_filename(rev, rev_path, sz_rev_path);
    return 1;
}

int
timecapsule_get_archive_blobs(const char *filename, int idx, int nblobs,
                              void *blobsptr, size_t szblob) {
    return timecap_query_object_refblobs(
            filename, idx, nblobs, blobsptr, szblob);
}

int
timecapsule_archive_new_revision(const char *filename,
                                 const void *ref, size_t szref,
                                 char *archived_path, size_t sz_archived_path) {
    char capsule_index[PATHLEN];
    int rev;

    if (!timecap_solve_revision_index(1, filename, capsule_index,
                                      sizeof(capsule_index)))
        return 0;
    rev = timecap_add_revision(filename, capsule_index);
    if (!rev)
        return 0;

    if (archived_path) {
        strlcpy(archived_path, capsule_index, sz_archived_path);
        timecap_convert_revision_filename(rev, archived_path, sz_archived_path);
    }
    return timecapsule_archive(filename, ref, szref);
}

