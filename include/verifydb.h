#ifndef INCLUDE_VEROFYDB_H
#define INCLUDE_VEROFYDB_H

#ifndef __cplusplus
# error "This file shall be used with C++ code only"
#endif

#include "verifydb.fbs.h"

using Bytes = std::vector<uint8_t>;

bool verifydb_getuser(const char *userid, int64_t generation, Bytes *buf,
                      const VerifyDb::GetReply **reply);

bool verifydb_getverify(int32_t vmethod, const char *vkey, Bytes *buf,
                        const VerifyDb::GetReply **reply);

bool is_entry_user_present(const VerifyDb::Entry *entry);

const char *verifydb_find_vmethod(const VerifyDb::GetReply *reply,
                                  int32_t vmethod);

std::optional<std::string> FormatVerify(int32_t vmethod, const char *vkey,
                                        bool ansi_color = false);

std::optional<std::string> FormatVerify(int32_t vmethod,
                                        const flatbuffers::String *vkey,
                                        bool ansi_color = false);

#endif
