// Sends mail to the internet, and puts mails in the mailboxes for the
// programs outside mbbsd.
#include "cmbbs.h"
#include "common.h"
#include "uflags.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* Copying the content into the spool takes a while for a large mail */
#define MAILSVC_TIMEOUT_MS  5000
/* Same as the limit of mail.svc */
#define MAILSVC_MAX_SIZE    (32 << 20)

// Turn CR and LF into spaces.
static const char *
one_line(char *buf, size_t size, const char *s)
{
    char *p;

    strlcpy(buf, s ? s : "", size);
    for (p = buf; *p; p++)
        if (*p == '\r' || *p == '\n')
            *p = ' ';
    return buf;
}

static int
mailsvc_request(const char *header, int fd, off_t len)
{
    char buf[8192];
    int sfd, n;
    off_t left = len;

    if ((sfd = svc_connect(MAILSVC_OUT_PATH)) < 0)
        return -1;
    n = (int)strlen(header);
    if (svc_write(sfd, header, n) != n)
        goto fail;
    while (fd >= 0 && left > 0) {
        ssize_t r = read(fd, buf, left < (off_t)sizeof(buf) ? (size_t)left : sizeof(buf));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0 || svc_write(sfd, buf, (size_t)r) != r)
            goto fail;
        left -= r;
    }
    shutdown(sfd, SHUT_WR);

    n = svc_read(sfd, buf, sizeof(buf) - 1, MAILSVC_TIMEOUT_MS);
    close(sfd);
    if (n <= 0)
        return -1;
    buf[n] = '\0';
    return (strncmp(buf, "OK", 2) == 0 &&
            (buf[2] == '\n' || buf[2] == ' ' || buf[2] == '\0')) ? 0 : -1;

fail:
    close(sfd);
    return -1;
}

int
mail_internet(const char *fpath, const char *title, const char *rcpt,
              const char *from)
{
    char header[1024], t[STRLEN * 2], r[STRLEN * 2], f[STRLEN];
    struct stat st;
    int fd, ret;

    if ((fd = open(fpath, O_RDONLY)) < 0)
        return -1;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) ||
        st.st_size > MAILSVC_MAX_SIZE) {
        close(fd);
        return -1;
    }
    if (snprintf(header, sizeof(header),
                 "SEND\nRcpt: %s\nFrom: %s\nSubject: %s\nLength: %lld\n\n",
                 one_line(r, sizeof(r), rcpt), one_line(f, sizeof(f), from),
                 one_line(t, sizeof(t), title), (long long)st.st_size) >=
        (int)sizeof(header)) {
        close(fd);
        return -1;
    }
    ret = mailsvc_request(header, fd, st.st_size);
    close(fd);
    return ret;
}

int
mail_ziphome(const char *userid, const char *rcpt)
{
    char header[512], r[STRLEN * 2], u[IDLEN + 1];

    if (snprintf(header, sizeof(header), "ZIPHOME\nUser: %s\nRcpt: %s\n\n",
                 one_line(u, sizeof(u), userid),
                 one_line(r, sizeof(r), rcpt)) >= (int)sizeof(header))
        return -1;
    return mailsvc_request(header, -1, 0);
}

/* Returns the uid (> 0) of userid, or < 1 if there is no such user. The
 * userid as registered goes to out_userid (IDLEN + 1 bytes), and whether
 * the user rejects mails from outside to out_rejects. */
int
get_mail_user(const char *userid, char *out_userid, int *out_rejects)
{
    userec_t xuser;
    int uid = passwd_load_user(userid, &xuser);

    if (uid < 1)
        return uid;
    strlcpy(out_userid, xuser.userid, IDLEN + 1);
    *out_rejects = REJECT_OUTTAMAIL(xuser) ? 1 : 0;
    return uid;
}

/* Starts a mail to the mailbox of userid (as registered), for the programs
 * outside mbbsd (which uses save_mailbox()): returns the file to write the
 * content to, in the storage encoding, or NULL if it failed. Finish it with
 * home_mail_end(). Paths are relative to BBSHOME. */
FILE *
home_mail_begin(home_mail_t *hm, const char *userid)
{
    struct stat st;

    memset(hm, 0, sizeof(*hm));
    strlcpy(hm->userid, userid, sizeof(hm->userid));
    sethomepath(hm->fpath, userid);
    if (stat(hm->fpath, &st) == -1) {
        if (Mkdir(hm->fpath) == -1)
            return NULL;
    } else if (!S_ISDIR(st.st_mode)) {
        return NULL;
    }

    if (stampfile(hm->fpath, &hm->fh) < 0)
        return NULL;
    if (!(hm->fp = fopen(hm->fpath, "w"))) {
        unlink(hm->fpath);
        return NULL;
    }
    return hm->fp;
}

/* Closes the file of the mail started by home_mail_begin(), to list it
 * (as home_mail_end() does). Returns 0, or -1 if writing it failed (and
 * the mail is removed). */
int
home_mail_close(home_mail_t *hm)
{
    int err = ferror(hm->fp);

    if (fclose(hm->fp) != 0)
        err = 1;
    hm->fp = NULL;
    if (err) {
        unlink(hm->fpath);
        return -1;
    }
    return 0;
}

/* Finishes the mail started by home_mail_begin(): lists it with owner and
 * title, in the memory encoding, and filemode (e.g. FILE_READ), and alerts
 * the user if online. Returns 0, or -1 if it failed (and the mail is
 * removed). */
int
home_mail_end(home_mail_t *hm, const char *owner, const char *title,
              int filemode)
{
    char dir[PATHLEN];
    userinfo_t *uentp;
    int uid, i;

    if (home_mail_close(hm) < 0)
        return -1;

    strlcpy(hm->fh.owner, owner, sizeof(hm->fh.owner));
    strlcpy(hm->fh.title, title, sizeof(hm->fh.title));
    hm->fh.filemode |= filemode;
    sethomedir(dir, hm->userid);
    if (append_fileheader(dir, &hm->fh) < 0) {
        unlink(hm->fpath);
        return -1;
    }

    // All the sessions, as sendalert_uid() of mbbsd does.
    if ((uid = searchuser(hm->userid, NULL)) > 0)
        for (i = 1; (uentp = search_ulistn(uid, i)); i++)
            uentp->alerts |= ALERT_NEW_MAIL;
    return 0;
}

/* Puts a mail in the mailbox of userid (as registered) and alerts the user
 * if online. The owner and title are in the memory encoding and content
 * in the storage encoding. Paths are relative to BBSHOME. Returns 0, or -1
 * if it failed. */
int
deliver_home_mail(const char *userid, const char *owner, const char *title,
                  const char *content, size_t len)
{
    home_mail_t hm;
    FILE *fp = home_mail_begin(&hm, userid);

    if (!fp)
        return -1;
    if (len && fwrite(content, 1, len, fp) != len) {
        fclose(fp);
        unlink(hm.fpath);
        return -1;
    }
    return home_mail_end(&hm, owner, title, 0);
}

/* deliver_home_mail() with the content copied from the file src, and
 * filemode for the listing (e.g. FILE_READ). */
int
deliver_home_mail_file(const char *userid, const char *owner,
                       const char *title, const char *src, int filemode)
{
    char buf[8192];
    home_mail_t hm;
    FILE *in, *fp;
    size_t n;

    if (!(in = fopen(src, "r")))
        return -1;
    if (!(fp = home_mail_begin(&hm, userid))) {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, fp) != n)
            break;
    if (ferror(in) || n > 0) {
        fclose(in);
        fclose(fp);
        unlink(hm.fpath);
        return -1;
    }
    fclose(in);
    return home_mail_end(&hm, owner, title, filemode);
}
