#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "cmbbs.h"
#include "cmsys.h"

#define OTPAUTH_FILENAME ".otpauth"
#define OTPSTEP_FILENAME ".otpauth.step"

int user_load_2fa(const char *userid, user_2fa_t *totp) {
    if (!userid || !totp)
        return 0;
    memset(totp, 0, sizeof(user_2fa_t));

    char path[PATHLEN];
    sethomefile(path, userid, OTPAUTH_FILENAME);

    FILE *fp = fopen(path, "r");
    if (!fp)
        return 0;

    char line[512];
    int backup_idx = 0;

    while (fgets(line, sizeof(line), fp)) {
        char *ptr = strchr(line, '\r');
        if (ptr)
            *ptr = '\0';
        ptr = strchr(line, '\n');
        if (ptr)
            *ptr = '\0';

        if (strncmp(line, "secret=", 7) == 0) {
            snprintf(totp->secret, sizeof(totp->secret), "%s", line + 7);
            totp->enabled = true;
        } else if (strncmp(line, "backup=", 7) == 0 && backup_idx < MAX_BACKUP_CODES) {
            char code[16];
            int used = 0;
            if (sscanf(line + 7, "%8s,%d", code, &used) >= 1) {
                snprintf(totp->backup_codes[backup_idx], sizeof(totp->backup_codes[backup_idx]), "%s", code);
                totp->backup_used[backup_idx] = used;
                backup_idx++;
            }
        }
    }
    fclose(fp);
    return totp->enabled;
}

int user_save_2fa(const char *userid, const user_2fa_t *totp) {
    if (!userid || !totp)
        return 0;

    char path[PATHLEN], tmppath[PATHLEN];
    sethomefile(path, userid, OTPAUTH_FILENAME);
    int len = snprintf(tmppath, sizeof(tmppath), "%s.XXXXXX", path);
    if (len < 0 || (size_t)len >= sizeof(tmppath))
        return 0;

    // Write a temporary file and rename it over the old one. Truncating
    // .otpauth in place would let a login reading it meanwhile find no
    // secret and turn 2FA off for the account. mkstemp creates the file
    // with mode 0600, so it is never readable by others.
    int fd = mkstemp(tmppath);
    if (fd < 0)
        return 0;
    FILE *fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(tmppath);
        return 0;
    }

    fprintf(fp, "version=1\n");
    fprintf(fp, "secret=%s\n", totp->secret);
    for (int i = 0; i < MAX_BACKUP_CODES; i++) {
        if (totp->backup_codes[i][0] != '\0') {
            fprintf(fp, "backup=%s,%d\n", totp->backup_codes[i], totp->backup_used[i]);
        }
    }
    int ok = !ferror(fp);
    if (fclose(fp) != 0)
        ok = 0;
    if (ok && rename(tmppath, path) != 0)
        ok = 0;
    if (!ok)
        unlink(tmppath);
    return ok;
}

int user_delete_2fa(const char *userid) {
    if (!userid)
        return 0;
    char path[PATHLEN];
    sethomefile(path, userid, OTPSTEP_FILENAME);
    unlink(path);
    sethomefile(path, userid, OTPAUTH_FILENAME);
    return unlink(path) == 0;
}

/* OTPSTEP_FILENAME holds the time step of the last accepted TOTP code as one
 * decimal number (RFC 6238 5.2 replay protection). */

/* Returns the stored step, or 0 if there is none or it cannot be read. A
 * garbled value is harmless: verify_2fa_step() ignores one in the future. */
static uint64_t
user_load_2fa_step(const char *userid)
{
    char path[PATHLEN], buf[32];
    uint64_t step = 0;

    sethomefile(path, userid, OTPSTEP_FILENAME);
    FILE *fp = fopen(path, "r");
    if (!fp)
        return 0;
    if (fgets(buf, sizeof(buf), fp))
        step = strtoull(buf, NULL, 10);
    fclose(fp);
    return step;
}

int user_save_2fa_step(const char *userid, uint64_t step) {
    if (!userid)
        return 0;

    char path[PATHLEN], tmppath[PATHLEN], buf[32];
    sethomefile(path, userid, OTPSTEP_FILENAME);
    int len = snprintf(tmppath, sizeof(tmppath), "%s.XXXXXX", path);
    if (len < 0 || (size_t)len >= sizeof(tmppath))
        return 0;

    // Write a temporary file and rename it over the old one, so a reader
    // never sees a partial value. mkstemp creates it with mode 0600.
    int fd = mkstemp(tmppath);
    if (fd < 0)
        return 0;
    len = snprintf(buf, sizeof(buf), "%llu\n", (unsigned long long)step);
    int ok = write(fd, buf, len) == len;
    if (close(fd) != 0)
        ok = 0;
    if (ok && rename(tmppath, path) != 0)
        ok = 0;
    if (!ok)
        unlink(tmppath);
    return ok;
}

int user_verify_2fa_or_backup(const char *userid, const char *input_code) {
    if (!userid || !input_code)
        return 0;

    user_2fa_t totp;
    if (!user_load_2fa(userid, &totp) || !totp.enabled)
        return 0;

    size_t len = strlen(input_code);

    // Case 1: 6-digit 2FA verification
    if (len == 6) {
        uint64_t matched = 0;
        if (!verify_2fa_step(totp.secret, input_code, 1,
                             user_load_2fa_step(userid), &matched))
            return 0;
        // The code is valid; failing to record it must not reject it. No
        // lock is taken, so logins never wait on each other; two logins at
        // the same moment may overwrite each other's step.
        user_save_2fa_step(userid, matched);
        return 1;
    }

    // Case 2: 8-digit Backup Recovery Code
    if (len == 8) {
        for (int i = 0; i < MAX_BACKUP_CODES; i++) {
            if (!totp.backup_used[i] && strcmp(totp.backup_codes[i], input_code) == 0) {
                // Consume backup code
                totp.backup_used[i] = 1;
                user_save_2fa(userid, &totp);
                return 1; // Verified successfully
            }
        }
    }
    return 0;
}
