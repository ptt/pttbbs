#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include "cmbbs.h"
#include "cmsys.h"

#define OTPAUTH_FILENAME ".otpauth"

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

    char path[PATHLEN];
    sethomefile(path, userid, OTPAUTH_FILENAME);

    FILE *fp = fopen(path, "w");
    if (!fp)
        return 0;

    chmod(path, 0600); // Only readable/writable by bbs user

    fprintf(fp, "version=1\n");
    fprintf(fp, "secret=%s\n", totp->secret);
    for (int i = 0; i < MAX_BACKUP_CODES; i++) {
        if (totp->backup_codes[i][0] != '\0') {
            fprintf(fp, "backup=%s,%d\n", totp->backup_codes[i], totp->backup_used[i]);
        }
    }
    fclose(fp);
    return 1;
}

int user_delete_2fa(const char *userid) {
    if (!userid)
        return 0;
    char path[PATHLEN];
    sethomefile(path, userid, OTPAUTH_FILENAME);
    return unlink(path) == 0;
}

int user_verify_2fa_or_backup(const char *userid, const char *input_code) {
    if (!userid || !input_code)
        return 0;

    user_2fa_t totp;
    if (!user_load_2fa(userid, &totp) || !totp.enabled)
        return 0;

    size_t len = strlen(input_code);

    // Case 1: 6-digit 2FA verification
    if (len == 6)
        return verify_2fa(totp.secret, input_code, 1);

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
