#ifndef UPGRADE_SGR66_H
#define UPGRADE_SGR66_H

#include "common.h"

/*
 * Fast read-only scan of a buffer:
 * Returns 1 if buf is valid Big5-UAO (and not already valid UTF-8).
 * Sets *out_has_dbcs to 1 if at least one Big5 DBCS char was seen.
 * Sets *out_has_split_sgr to 1 if at least one DBCS char split by ANSI SGR was seen.
 * Returns 0 if buf is invalid Big5-UAO or is already valid UTF-8.
 */
int scan_big5_uao(const uint8_t *buf, size_t len,
                  int *out_has_dbcs, int *out_has_split_sgr);

/*
 * Converts a validated Big5-UAO buffer:
 * - Merges any split dual-color ANSI SGR sequences into SGR 66.
 * - If to_utf8 is non-zero, converts Big5-UAO DBCS characters to UTF-8.
 * - If to_utf8 is 0, preserves Big5-UAO DBCS bytes.
 */
int convert_big5_sgr66(const uint8_t *buf, size_t len,
                       int to_utf8, bytebuf_t *out);

#endif /* UPGRADE_SGR66_H */
