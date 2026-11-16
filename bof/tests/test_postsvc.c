/*
 * Tests of the post.svc backend of bof, against a fake post.svc that
 * answers each command with a canned response and records what it got.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include "bbs.h"
#include "bof_test.h"
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

int bof_test_failures;

#define BOARD "Test"
#define BDIR "boards/T/" BOARD
#define DIRPATH BDIR "/.DIR"
#define SOCK "post.svc.sock"   /* bof_postsvc_set_socket() */

/* ------------------------------------------------------------------------
 * The fake post.svc
 * ------------------------------------------------------------------------ */

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char got[64][256];       // the first line of each request
static int ngot;
static char replies[32][2][256];    // command -> response
static int nreplies;
static int listen_fd = -1;

static void
reply(const char *cmd, const char *resp)
{
    pthread_mutex_lock(&lock);
    for (int i = 0; i < nreplies; i++)
        if (strcmp(replies[i][0], cmd) == 0) {
            strlcpy(replies[i][1], resp, sizeof(replies[i][1]));
            pthread_mutex_unlock(&lock);
            return;
        }
    strlcpy(replies[nreplies][0], cmd, sizeof(replies[0][0]));
    strlcpy(replies[nreplies][1], resp, sizeof(replies[0][1]));
    nreplies++;
    pthread_mutex_unlock(&lock);
}

static void
forget(void)
{
    pthread_mutex_lock(&lock);
    ngot = 0;
    pthread_mutex_unlock(&lock);
}

// Whether a request starting with prefix was received.
static int
received(const char *prefix)
{
    int found = 0;

    pthread_mutex_lock(&lock);
    for (int i = 0; i < ngot && !found; i++)
        found = strncmp(got[i], prefix, strlen(prefix)) == 0;
    pthread_mutex_unlock(&lock);
    return found;
}

static void *
serve(void *arg GCC_UNUSED)
{
    int fd;

    while ((fd = accept(listen_fd, NULL, NULL)) >= 0) {
        char buf[8192], cmd[64] = "", resp[300] = "ERR unknown\n";
        size_t len = 0;
        struct pollfd p = { .fd = fd, .events = POLLIN };
        ssize_t n;

        // The request, with its payloads: until the client goes quiet.
        while (len < sizeof(buf) - 1 && poll(&p, 1, 100) > 0 &&
               (n = read(fd, buf + len, sizeof(buf) - 1 - len)) > 0)
            len += n;
        buf[len] = '\0';
        sscanf(buf, "%63s", cmd);
        pthread_mutex_lock(&lock);
        if (ngot < 64) {
            char *nl = strchr(buf, '\n');
            if (nl)
                *nl = '\0';
            strlcpy(got[ngot++], buf, sizeof(got[0]));
        }
        for (int i = 0; i < nreplies; i++)
            if (strcmp(replies[i][0], cmd) == 0)
                snprintf(resp, sizeof(resp), "%s\n", replies[i][1]);
        pthread_mutex_unlock(&lock);
        if (write(fd, resp, strlen(resp)) < 0)
            perror("write");
        close(fd);
    }
    return NULL;
}

static void
start_server(void)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    pthread_t t;

    strlcpy(addr.sun_path, SOCK, sizeof(addr.sun_path));
    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0 ||
        bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(listen_fd, 8) != 0 ||
        pthread_create(&t, NULL, serve, NULL) != 0) {
        perror("fake post.svc");
        exit(1);
    }
    pthread_detach(t);
}

/* ------------------------------------------------------------------------
 * The board
 * ------------------------------------------------------------------------ */

static fileheader_t
make_post(const char *filename)
{
    fileheader_t fh;
    char path[PATHLEN];
    FILE *fp;

    memset(&fh, 0, sizeof(fh));
    strlcpy(fh.filename, filename, sizeof(fh.filename));
    strlcpy(fh.owner, "alice", sizeof(fh.owner));
    strlcpy(fh.title, "hello", sizeof(fh.title));
    strlcpy(fh.date, " 1/01", sizeof(fh.date));
    snprintf(path, sizeof(path), BDIR "/%s", filename);
    fp = fopen(path, "w");
    fprintf(fp, "the content of %s\n", filename);
    fclose(fp);
    fp = fopen(DIRPATH, "a");
    fwrite(&fh, sizeof(fh), 1, fp);
    fclose(fp);
    return fh;
}

static int
read_dir(fileheader_t *recs, int max)
{
    FILE *fp = fopen(DIRPATH, "r");
    int n;

    if (!fp)
        return 0;
    n = fread(recs, sizeof(fileheader_t), max, fp);
    fclose(fp);
    return n;
}

static const bof_actor_t alice = {
    .name = "alice", .from = "127.0.0.1", .bbs_local_user = 42,
};

/* ------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

static void
test_create_post(void)
{
    fileheader_t fh, recs[4];
    FILE *fp = fopen("draft.tmp", "w");

    fprintf(fp, "a new post\n");
    fclose(fp);
    memset(&fh, 0, sizeof(fh));
    strlcpy(fh.filename, "M.100.A.AAA", sizeof(fh.filename));
    strlcpy(fh.owner, "alice", sizeof(fh.owner));
    strlcpy(fh.title, "new", sizeof(fh.title));

    bof_create_post_t req = {
        .community = BOARD, .bbs_dir = DIRPATH, .bbs_header = &fh,
        .bbs_content_path = "draft.tmp",
    };

    // post.svc fails: nothing in the list.
    forget();
    reply("POST", "ERR down");
    EXPECT_INT(bof_create_post(&alice, &req, NULL),
               BOF_ERR_COULDNT_CREATE_POST);
    EXPECT_INT(read_dir(recs, 4), 0);

    forget();
    reply("POST", "OK 1");
    EXPECT_OK(bof_create_post(&alice, &req, NULL));
    EXPECT(received("POST " BOARD " M.100.A.AAA alice 42 "));
    EXPECT_INT(read_dir(recs, 4), 1);
    EXPECT_STR(recs[0].filename, "M.100.A.AAA");

    // A crosspost names where it came from.
    strlcpy(fh.filename, "M.101.A.BBB", sizeof(fh.filename));
    req.bbs_crosspost_community = "Other";
    req.bbs_crosspost_file = "M.99.A.CCC";
    reply("CROSSPOST", "OK");
    forget();
    EXPECT_OK(bof_create_post(&alice, &req, NULL));
    EXPECT(received("CROSSPOST Other M.99.A.CCC " BOARD " M.101.A.BBB alice 42 "));
    unlink("draft.tmp");
}

static void
test_comments(void)
{
    fileheader_t fh = make_post("M.200.A.AAA"), updated, recs[8];
    int n = read_dir(recs, 8);
    bof_create_comment_t req = {
        .post = {
            .community = BOARD, .bbs_dir = DIRPATH, .bbs_index = n,
            .bbs_header = &fh,
        },
        .content = "nice", .bbs_text = "nice", .bbs_vote = 1,
    };
    bof_comment_response_t res = { .bbs_header = &updated };

    forget();
    reply("COMMENT", "OK 3 1700000000 1 NEW 99");
    EXPECT_OK(bof_create_comment(&alice, &req, &res));
    EXPECT(received("COMMENT " BOARD " M.200.A.AAA alice 42 127.0.0.1 "));
    EXPECT_INT(res.bbs_floor, 3);
    EXPECT_INT(res.bbs_merged, 0);
    EXPECT_INT(updated.comments, 1);
    EXPECT_INT(updated.modified, 1700000000);
    read_dir(recs, 8);
    EXPECT_INT(recs[n - 1].comments, 1);

    // Merged into the previous one: no new comment.
    fh = updated;
    reply("COMMENT", "OK 3 1700000001 1 MERGED 99");
    EXPECT_OK(bof_create_comment(&alice, &req, &res));
    EXPECT_INT(res.bbs_merged, 1);
    EXPECT_INT(updated.comments, 1);
    EXPECT_INT(updated.modified, 1700000001);

    // post.svc fails: the record stays.
    fh = updated;
    reply("COMMENT", "ERR down");
    EXPECT_INT(bof_create_comment(&alice, &req, &res),
               BOF_ERR_COULDNT_CREATE_COMMENT);
    read_dir(recs, 8);
    EXPECT_INT(recs[n - 1].modified, 1700000001);
}

static void
test_votes(void)
{
    fileheader_t fh = make_post("M.300.A.AAA"), updated, recs[8];
    int n = read_dir(recs, 8);
    bof_create_post_like_t req = {
        .post = {
            .community = BOARD, .bbs_dir = DIRPATH, .bbs_index = n,
            .bbs_header = &fh,
        },
        .score = 1,
    };
    bof_post_like_response_t res = { .bbs_header = &updated };

    forget();
    reply("VOTE_FILE", "OK 1 0");
    EXPECT_OK(bof_create_post_like(&alice, &req, &res));
    EXPECT(received("VOTE_FILE " BOARD " M.300.A.AAA alice 42 1 127.0.0.1 "));
    EXPECT_INT(res.bbs_score_delta, 1);
    EXPECT_INT(updated.score, 1);

    // The same vote again changes nothing.
    fh = updated;
    reply("VOTE_FILE", "OK 0 0");
    EXPECT_OK(bof_create_post_like(&alice, &req, &res));
    EXPECT_INT(res.bbs_score_delta, 0);
    read_dir(recs, 8);
    EXPECT_INT(recs[n - 1].score, 1);

    // Changed to a boo: one up less, one down more.
    reply("VOTE_FILE", "OK -1 1");
    req.score = -1;
    EXPECT_OK(bof_create_post_like(&alice, &req, &res));
    EXPECT_INT(res.bbs_score_delta, -2);
    read_dir(recs, 8);
    EXPECT_INT(recs[n - 1].score, -1);
}

static void
test_body_and_refresh(void)
{
    fileheader_t fh = make_post("M.400.A.AAA");
    bof_post_ref_t post = {
        .community = BOARD, .bbs_dir = DIRPATH, .bbs_header = &fh,
    };
    bof_post_version_t ver = { 0 };

    // The version of the body is its time in post.svc.
    forget();
    reply("FETCH_POST_FILE", "OK 1700000500");
    EXPECT_OK(bof_bbs_get_post_body(&post, "edit.tmp", &ver));
    EXPECT(received("FETCH_POST_FILE " BOARD " M.400.A.AAA edit.tmp"));
    EXPECT_INT((long long)ver.hash, 1700000500);
    EXPECT_INT(bof_bbs_post_version_matches(&post, &ver), 1);
    reply("FETCH_POST_FILE", "OK 1700000600");
    EXPECT_INT(bof_bbs_post_version_matches(&post, &ver), 0);

    // The file is there (post.svc writes it): not rendered again.
    forget();
    reply("RENDER_FILE", "OK");
    char path[PATHLEN];
    EXPECT_OK(bof_bbs_get_post_file(&post, path, sizeof(path)));
    EXPECT_STR(path, BDIR "/M.400.A.AAA");
    EXPECT(!received("RENDER_FILE"));
    // Missing: rendered.
    unlink(BDIR "/M.400.A.AAA");
    EXPECT_OK(bof_bbs_get_post_file(&post, path, sizeof(path)));
    EXPECT(received("RENDER_FILE " BOARD " M.400.A.AAA " BDIR "/M.400.A.AAA modern"));
}

static void
test_comment_lines_and_revisions(void)
{
    fileheader_t fh = make_post("M.600.A.AAA");
    bof_comment_t cs[4];
    bof_comment_revision_t revs[4];
    bof_get_comments_t req = {
        .post = { .community = BOARD, .bbs_dir = DIRPATH, .bbs_header = &fh },
        .limit = 4,
    };
    bof_get_comments_response_t res = { .comments = cs };

    // A comment of several lines keeps them.
    reply("COMMENTS_FILE",
          "OK 1\n1\tbob\t1700000000\t-\t1\t-\t9\tline one\tline two");
    EXPECT_OK(bof_get_comments(&req, &res));
    EXPECT_INT(res.count, 1);
    EXPECT_STR(cs[0].content, "line one\nline two");
    EXPECT_STR(cs[0].creator, "bob");
    EXPECT_INT(cs[0].bbs_vote, 1);

    bof_get_comment_revisions_t rreq = {
        .post = req.post, .bbs_floor = 1, .limit = 4,
    };
    bof_get_comment_revisions_response_t rres = { .revisions = revs };
    reply("COMMENT_HISTORY_FILE",
          "OK 2\n1\t1700000000\tbob\tedit\t-\told\tlines\n"
          "2\t1700000100\tboss\tdelete\tspam\tbad");
    forget();
    EXPECT_OK(bof_get_comment_revisions(&rreq, &rres));
    EXPECT(received("COMMENT_HISTORY_FILE " BOARD " M.600.A.AAA 1"));
    EXPECT_INT(rres.count, 2);
    EXPECT_INT(revs[0].number, 1);
    EXPECT_STR(revs[0].content, "old\nlines");
    EXPECT_INT(revs[0].deleted, 0);
    EXPECT_STR(revs[1].editor, "boss");
    EXPECT_INT(revs[1].deleted, 1);
    EXPECT_STR(revs[1].reason, "spam");
    EXPECT_INT(revs[1].edited_at, 1700000100);
}

static int
select_500(const fileheader_t *fh, int index GCC_UNUSED, void *ctx GCC_UNUSED)
{
    return strncmp(fh->filename, "M.500.", 6) == 0;
}

static void
test_purge(void)
{
    fileheader_t recs[16];
    bof_actor_t sys = { .name = "system" };
    int n, count;

    make_post("M.500.A.AAA");
    make_post("M.501.A.AAA");
    n = read_dir(recs, 16);
    bof_purge_posts_t req = {
        .community = BOARD, .bbs_dir = DIRPATH, .bbs_selected = select_500,
        .reason = "expire",
    };
    // post.svc keeps it as deleted; the cache drops it.
    forget();
    reply("DELETE_POST_FILE", "OK");
    EXPECT_OK(bof_purge_posts(&sys, &req, &count));
    EXPECT_INT(count, 1);
    EXPECT(received("DELETE_POST_FILE " BOARD " M.500.A.AAA system 0 expire"));
    EXPECT(!received("DELETE_POST_FILE " BOARD " M.501"));
    EXPECT_INT(read_dir(recs, 16), n - 1);
    EXPECT(access(BDIR "/M.500.A.AAA", F_OK) != 0);
}

static void
test_reports_stay_in_files(void)
{
    // post.svc does not keep reports: the files backend serves them.
    EXPECT(bof_storage_postsvc.create_report == bof_storage_files.create_report);
    EXPECT(bof_storage_postsvc.write_modlog == bof_storage_files.write_modlog);
    EXPECT_STR(bof_storage_postsvc.name, "postsvc");
}

static void
test_create_named_and_from_body(void)
{
    fileheader_t fh, got;
    bof_post_response_t res = { .bbs_header = &got };
    char path[PATHLEN], buf[1024];
    FILE *fp;
    size_t n;

    // No name yet and only the body (a Lemmy client): named by bof,
    // written as a post of the BBS, sent to post.svc by that name.
    memset(&fh, 0, sizeof(fh));
    strlcpy(fh.owner, "alice", sizeof(fh.owner));
    strlcpy(fh.title, "from the web", sizeof(fh.title));
    bof_create_post_t req = {
        .community = BOARD, .bbs_dir = DIRPATH, .bbs_header = &fh,
        .body = "line one",
    };
    forget();
    reply("POST", "OK 2");
    EXPECT_OK(bof_create_post(&alice, &req, &res));
    EXPECT(strncmp(got.filename, "M.", 2) == 0);
    snprintf(buf, sizeof(buf), "POST " BOARD " %s alice 42 ", got.filename);
    EXPECT(received(buf));
    snprintf(path, sizeof(path), BDIR "/%s", got.filename);
    fp = fopen(path, "r");
    n = fp ? fread(buf, 1, sizeof(buf) - 1, fp) : 0;
    if (fp)
        fclose(fp);
    buf[n] = '\0';
    EXPECT(strstr(buf, "line one\n") != NULL);
}

static void
test_ids_and_revisions(void)
{
    fileheader_t fh = make_post("M.700.A.AAA");
    bof_post_ref_t post = {
        .community = BOARD, .bbs_dir = DIRPATH, .bbs_index = 1,
        .bbs_header = &fh,
    };
    bof_post_id_t id = 0;
    bof_post_key_t key;
    bof_community_id_t cid = 0;
    char path[PATHLEN];
    int revs = 0;

    // Post IDs and their keys are post.svc's.
    reply("GET_POST", "OK 42 0 " BOARD " M.700.A.AAA alice 42 1 0 0 0 0 5 9");
    EXPECT_OK(bof_get_post_id(&post, &id));
    EXPECT_INT(id, 42);
    reply("GET_POST_ID", "OK 42 0 " BOARD " M.700.A.AAA alice 42 1 0 0 0 0 5 9");
    EXPECT_OK(bof_get_post_key(42, &key));
    EXPECT_STR(key.community, BOARD);
    EXPECT_STR(key.bbs_post_file, "M.700.A.AAA");
    reply("GET_POST_ID", "ERR post not found");
    EXPECT_INT(bof_get_post_key(43, &key), BOF_ERR_COULDNT_FIND_POST);
    // So are those of communities.
    reply("GET_COMMUNITY", "OK 40\nEncoding: utf-8\nID: 7\nName: " BOARD);
    EXPECT_OK(bof_bbs_community_id(BOARD, &cid));
    EXPECT_INT(cid, 7);

    // The versions post.svc keeps, oldest first; one fetched by its rev.
    reply("POST_HISTORY_FILE",
          "OK 2\n3\t1700000000\talice\told title\n5\t1700000100\tbob");
    EXPECT_OK(bof_bbs_get_post_revisions(&post, &revs));
    EXPECT_INT(revs, 2);
    forget();
    EXPECT_OK(bof_bbs_get_post_revision_file(&post, 2, path, sizeof(path)));
    EXPECT(received("POST_HISTORY_FILE " BOARD " M.700.A.AAA 5 "));
    EXPECT_INT(bof_bbs_get_post_revision_file(&post, 3, path, sizeof(path)),
               BOF_ERR_COULDNT_FIND_POST);
    // Who saved them and when.
    bof_bbs_post_revision_t info[4];
    bof_bbs_list_post_revisions_t lreq = { .post = post, .limit = 4 };
    bof_bbs_list_post_revisions_response_t lres = { .revisions = info };
    EXPECT_OK(bof_bbs_list_post_revisions(&lreq, &lres));
    EXPECT_INT(lres.bbs_total, 2);
    EXPECT_INT(lres.count, 2);
    EXPECT_INT(info[1].number, 2);
    EXPECT_INT(info[1].edited_at, 1700000100);
    EXPECT_STR(info[1].editor, "bob");
    EXPECT_STR(info[0].name, "old title");
    lreq.start = 1;
    EXPECT_OK(bof_bbs_list_post_revisions(&lreq, &lres));
    EXPECT_INT(lres.count, 1);
    EXPECT_STR(info[0].editor, "bob");
    // None there: the time capsule of the files (none either).
    reply("POST_HISTORY_FILE", "OK 0");
    EXPECT_OK(bof_bbs_get_post_revisions(&post, &revs));
    EXPECT_INT(revs, 0);
}

// The fixes of what came from pttbbs.cmd: the record of a post made while
// the cache could not be written, comments by page, and addresses only
// where the board shows them.
static void
test_cache_failure_pages_and_addresses(void)
{
    fileheader_t fh, got;
    bof_post_response_t res = { .bbs_header = &got };
    bof_comment_t cs[4];

    // The board in the board cache, as the BBS keeps it.
    SHM = calloc(1, sizeof(SHM_t));
    bcache = SHM->bcache;
    memset(SHM->hash_head, 0xff, sizeof(SHM->hash_head));
    strlcpy(bcache[0].brdname, BOARD, sizeof(bcache[0].brdname));
    SHM->Bnumber = 1;
    SHM->bsorted[0][0] = 0;

    // post.svc has the post, the list of the cache cannot take it: the
    // record is still given, as post.svc has it.
    mkdir(BDIR "/broken.DIR", 0755);
    memset(&fh, 0, sizeof(fh));
    strlcpy(fh.owner, "alice", sizeof(fh.owner));
    strlcpy(fh.title, "cached later", sizeof(fh.title));
    bof_create_post_t req = {
        .community = BOARD, .bbs_dir = BDIR "/broken.DIR", .bbs_header = &fh,
        .body = "text",
    };
    memset(&got, 0, sizeof(got));
    reply("POST", "OK 3");
    EXPECT_OK(bof_create_post(&alice, &req, &res));
    EXPECT(strncmp(got.filename, "M.", 2) == 0);
    EXPECT_STR(got.title, "cached later");
    EXPECT(access(BOF_STORAGE_LOG, F_OK) == 0);    // to bring in step

    // Comments by page: of all, from the floor; of one person, counted.
    fileheader_t p = make_post("M.700.A.AAA");
    bof_get_comments_t get = {
        .post = { .community = BOARD, .bbs_dir = DIRPATH, .bbs_header = &p },
        .page = 3, .limit = 2,
    };
    bof_get_comments_response_t gres = { .comments = cs };
    reply("COMMENTS_FILE",
          "OK 2\n5\tbob\t1700000000\t1.2.3.4\t1\t-\t9\tfive\n"
          "6\tbob\t1700000000\t1.2.3.4\t1\t-\t10\tsix");
    forget();
    EXPECT_OK(bof_get_comments(&get, &gres));
    EXPECT(received("COMMENTS_FILE " BOARD " M.700.A.AAA 5 2 -"));
    EXPECT_INT(gres.count, 2);
    EXPECT_INT(cs[0].bbs_floor, 5);
    // The address is private on a board that does not show it.
    EXPECT(strstr(cs[0].bbs_tail, "1.2.3.4") == NULL);
    get.page = 2;
    get.bbs_creator = "bob";
    forget();
    EXPECT_OK(bof_get_comments(&get, &gres));
    EXPECT(received("COMMENTS_FILE " BOARD " M.700.A.AAA 1 4 bob"));
    EXPECT_INT(gres.count, 0);  // the first two of bob are skipped
    // Shown where the board shows it.
    bcache[0].brdattr |= BRD_COMMENT_SHOWIP;
    get.page = 1;
    get.bbs_creator = NULL;
    EXPECT_OK(bof_get_comments(&get, &gres));
    EXPECT(strstr(cs[0].bbs_tail, "1.2.3.4") != NULL);

    free(SHM);
    SHM = NULL;
    bcache = NULL;
}

int
main(void)
{
    char dir[] = "/tmp/bof_test_XXXXXX", cmd[PATHLEN];

    if (!mkdtemp(dir) || chdir(dir) != 0) {
        perror("setup");
        return 1;
    }
    mkdir("boards", 0755);
    mkdir("boards/T", 0755);
    mkdir(BDIR, 0755);
    mkdir("log", 0755);
    start_server();
    bof_postsvc_set_socket(SOCK);
    bof_storage_reset();
    bof_storage_set(&bof_storage_postsvc, NULL);

    RUN(test_create_post);
    RUN(test_comments);
    RUN(test_votes);
    RUN(test_body_and_refresh);
    RUN(test_purge);
    RUN(test_comment_lines_and_revisions);
    RUN(test_reports_stay_in_files);
    RUN(test_create_named_and_from_body);
    RUN(test_ids_and_revisions);
    RUN(test_cache_failure_pages_and_addresses);

    bof_storage_reset();
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0)
        fprintf(stderr, "could not remove %s\n", dir);
    return bof_test_report();
}
