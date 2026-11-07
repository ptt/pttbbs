package storage

import (
	"database/sql"
	"fmt"
	"path/filepath"
	"sort"
	"testing"
	"time"

	"pttbbs/post/model"
)

// dumpMetaRows returns every SQLite row of posts and crossposts as "key=value"
// strings. Crosspost IDs are left out: a rebuild renumbers them, and nothing
// looks crossposts up by ID.
func dumpMetaRows(t *testing.T, db *sql.DB) map[string]string {
	t.Helper()
	out := map[string]string{}
	rows, err := db.Query(`SELECT id, parent_id, community, post_file, title, creator_name, creator_id, created_at, modified, filemode,
		upvotes, downvotes, num_comments, num_crossposts, encoding, deleted, removed, deleted_at, mod_person_name, mod_person_id, reason, money
		FROM posts`)
	if err != nil {
		t.Fatal(err)
	}
	defer rows.Close()
	cols, _ := rows.Columns()
	for rows.Next() {
		vals := make([]any, len(cols))
		ptrs := make([]any, len(cols))
		for i := range vals {
			ptrs[i] = &vals[i]
		}
		if err := rows.Scan(ptrs...); err != nil {
			t.Fatal(err)
		}
		for i, c := range cols {
			out[fmt.Sprintf("post %v %s", vals[0], c)] = fmt.Sprint(vals[i])
		}
	}
	xrows, err := db.Query(`SELECT source_post_id, target_community, target_post_file, creator_name, creator_id, created_at FROM crossposts`)
	if err != nil {
		t.Fatal(err)
	}
	defer xrows.Close()
	for xrows.Next() {
		var src, cid, ctime int64
		var tc, tf, cn string
		if err := xrows.Scan(&src, &tc, &tf, &cn, &cid, &ctime); err != nil {
			t.Fatal(err)
		}
		out[fmt.Sprintf("crosspost %d->%s/%s", src, tc, tf)] = fmt.Sprint(cn, " ", cid, " ", ctime)
	}
	return out
}

// TestRebuildReproducesLiveSQLite runs the write paths that touch SQLite and
// checks that RebuildSQLiteFromPebble reproduces exactly the same rows, i.e.
// that SQLite holds nothing Pebble cannot give back.
//
// Known, accepted gaps that are deliberately not exercised here:
//   - UpdatePostTitle changes the title and modified time only in SQLite.
//   - Deleted posts imported from .DIR without an article file keep their
//     .DIR counters only in SQLite; a rebuild resets them to 0.
func TestRebuildReproducesLiveSQLite(t *testing.T) {
	dir := t.TempDir()
	e, err := OpenEngine(Config{
		DataDir:        filepath.Join(dir, "db"),
		CacheDir:       filepath.Join(dir, "cache"),
		CacheSizeMB:    16,
		FlushSeconds:   3600,
		FilterEncoding: "utf-8",
	})
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()

	must := func(err error) {
		t.Helper()
		if err != nil {
			t.Fatal(err)
		}
	}
	now := time.Now().Unix() - 3600
	create := func(file, title, author string) *model.Post {
		t.Helper()
		p, err := e.CreatePost(&model.Post{Community: "board", PostFile: file, Title: title, CreatorName: author, CreatedAt: now, Content: "body of " + file})
		must(err)
		return p
	}
	comment := func(p *model.Post, author string, at int64) {
		t.Helper()
		_, err := e.AddComment(&model.Comment{PostID: p.ID, CreatorName: author, Content: "comment by " + author, CreatedAt: at})
		must(err)
	}

	// A title with line breaks must not be able to forge Pebble headers.
	injected := create("M.1.A.001", "evil\nDeleted: true\r\nReason: x", "author")
	if injected.Title != "evil Deleted: true Reason: x" {
		t.Errorf("sanitized title = %q", injected.Title)
	}

	// Comments, merged comments, votes, and comment deletion/edits.
	p := create("M.1.A.002", "comments", "author")
	comment(p, "a", now+1)
	comment(p, "a", now+2) // Merged into the previous comment.
	comment(p, "b", now+1000)
	comment(p, "c", now+2000)
	comment(p, "d", now+3000)
	_, err = e.UpdateComment(p.ID, 2, "edited", "b", 0)
	must(err)
	_, err = e.DeleteComment(p.ID, 3, "c", 0, "")
	must(err)
	_, err = e.DeleteComment(p.ID, 4, "mod", 0, "spam")
	must(err)
	must(e.UndeleteComment(p.ID, 4))
	for _, v := range []struct {
		who  string
		vote model.VoteType
	}{{"a", model.VoteUp}, {"b", model.VoteUp}, {"c", model.VoteDown}, {"b", model.VoteDown}, {"d", model.VoteUp}, {"d", model.VoteNeutral}} {
		_, _, err := e.VotePost(p.ID, v.who, 0, v.vote, now, "127.0.0.1")
		must(err)
	}

	// Comments purged as spam stay as tombstones.
	spam := create("M.1.A.003", "spam target", "author")
	comment(spam, "spammer", time.Now().Unix())
	e.FlushDirtyDeltas()
	_, err = e.PurgeUserComments("spammer", time.Hour)
	must(err)

	// Content edits, deletion, restore, meta and votes reset.
	edited := create("M.1.A.004", "edited", "author")
	_, _, err = e.UpdatePost(edited.ID, "edited title\n", "new body", "author", 0, 0)
	must(err)
	removed := create("M.1.A.005", "removed", "author")
	_, err = e.DeletePost(removed.ID, "mod", 0, "rule 1")
	must(err)
	restored := create("M.1.A.006", "restored", "author")
	_, err = e.DeletePost(restored.ID, "author", 0, "")
	must(err)
	must(e.UndeletePost(restored.ID))
	must(e.SetPostMeta("board", "M.1.A.006", 0x10, 300))
	reset := create("M.1.A.007", "votes reset", "author")
	_, _, err = e.VotePost(reset.ID, "a", 0, model.VoteUp, now, "")
	must(err)
	must(e.ResetVotes(reset.ID))

	// Crossposts.
	src := create("M.1.A.008", "crossposted", "author")
	_, _, err = e.AddCrosspost("board", src.PostFile, "other", "M.9.A.001", "op", 0, now)
	must(err)
	_, _, err = e.AddCrosspost("board", src.PostFile, "other2", "M.9.A.002", "op", 0, now)
	must(err)

	// Purged posts disappear from both stores.
	purged := create("M.1.A.009", "purged", "author")
	comment(purged, "a", now)
	must(e.PurgePost(purged.ID))

	// Imported legacy posts with comments and per-user votes.
	imported := &model.Post{Community: "board", PostFile: "M.1.A.010", Title: "imported\r\n", CreatorName: "author",
		CreatedAt: now, Modified: now, Upvotes: 1, Downvotes: 1, Content: "legacy body"}
	must(e.ImportPostBatch([]*ImportedPostData{{
		Post: imported,
		Comments: []*model.Comment{
			{CreatorName: "a", Content: "推", CreatedAt: now, LegacyType: model.LegacyTypePush},
			{CreatorName: "a", Content: "推", CreatedAt: now + 1, LegacyType: model.LegacyTypePush},
			{CreatorName: "b", Content: "噓", CreatedAt: now + 2, LegacyType: model.LegacyTypeBoo},
		},
		Votes: []*model.VoteRecord{
			{PersonName: "a", Vote: model.VoteUp, CreatedAt: now + 1},
			{PersonName: "b", Vote: model.VoteDown, CreatedAt: now + 2},
		},
		Crossposts: []*model.CrosspostRecord{{TargetCommunity: "old", TargetPostFile: "M.0.A.001", CreatorName: "x", CreatedAt: now}},
	}}, ""))

	e.FlushDirtyDeltas()
	live := dumpMetaRows(t, e.metaDB)
	if got := live[fmt.Sprintf("post %d num_comments", p.ID)]; got != "4" {
		t.Errorf("live num_comments with tombstones = %s, want 4", got)
	}
	if _, _, err := e.RebuildSQLiteFromPebble(); err != nil {
		t.Fatalf("RebuildSQLiteFromPebble: %v", err)
	}
	rebuilt := dumpMetaRows(t, e.metaDB)

	var diffs []string
	for k, v := range live {
		if rebuilt[k] != v {
			diffs = append(diffs, fmt.Sprintf("%s: live=%q rebuilt=%q", k, v, rebuilt[k]))
		}
	}
	for k, v := range rebuilt {
		if _, ok := live[k]; !ok {
			diffs = append(diffs, fmt.Sprintf("%s: only after rebuild=%q", k, v))
		}
	}
	sort.Strings(diffs)
	for _, d := range diffs {
		t.Error(d)
	}
}
