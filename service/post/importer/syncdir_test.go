package importer

import (
	"bytes"
	"encoding/binary"
	"math"
	"os"
	"path/filepath"
	"testing"
	"time"

	"pttbbs/big5uao"
	"pttbbs/post/model"
	"pttbbs/post/storage"
)

func TestScoreAndCommentsClamp(t *testing.T) {
	if got := ScoreFromVotes(150, 20); got != 130 {
		t.Errorf("ScoreFromVotes(150, 20) = %d, want 130", got)
	}
	if got := ScoreFromVotes(3, 10); got != -7 {
		t.Errorf("ScoreFromVotes(3, 10) = %d, want -7", got)
	}
	if got := ScoreFromVotes(100000, 0); got != math.MaxInt16 {
		t.Errorf("ScoreFromVotes(100000, 0) = %d, want %d", got, math.MaxInt16)
	}
	if got := ScoreFromVotes(0, 100000); got != math.MinInt16 {
		t.Errorf("ScoreFromVotes(0, 100000) = %d, want %d", got, math.MinInt16)
	}
	if got := CommentsFromCount(70000); got != math.MaxUint16 {
		t.Errorf("CommentsFromCount(70000) = %d, want %d", got, math.MaxUint16)
	}
	if got := CommentsFromCount(-1); got != 0 {
		t.Errorf("CommentsFromCount(-1) = %d, want 0", got)
	}
}

func TestDeletedTitle(t *testing.T) {
	deleted := func(creatorName string, creatorID model.PersonID, byName string, byID model.PersonID, reason string, filemode int) model.Post {
		p := model.Post{CreatorName: creatorName, CreatorID: creatorID, Filemode: filemode}
		p.Deletion.Delete(creatorName, creatorID, byName, byID, reason, 0)
		return p
	}
	cases := []struct {
		name string
		post model.Post
		want string
	}{
		{"self", deleted("userA", 0, "userA", 0, "", 0), "(本文已被刪除) [userA]"},
		{"self by ID", deleted("userA", 7, "sysop", 7, "", 0), "(本文已被刪除) [userA]"},
		{"moderator", deleted("userA", 0, "bm", 0, "", 0), "(本文已被刪除) <userA>"},
		{"other by ID", deleted("userA", 7, "userA", 8, "", 0), "(本文已被刪除) <userA>"},
		{"anonymous", deleted("userA", 0, "userA", 0, "", model.FileAnonymous), "(本文已被刪除)"},
		{"no creator", deleted("", 0, "bm", 0, "", 0), "(本文已被刪除)"},
		{"internal reason", deleted("userA", 0, "bm", 0, "del_range", 0), "(本文已被刪除) <userA>"},
		{"moderator title", deleted("userA", 0, "bm", 0, "(已被bm刪除) <userA>", 0), "(已被bm刪除) <userA>"},
	}
	for _, c := range cases {
		got := DeletedTitle(&c.post)
		if got != c.want {
			t.Errorf("%s: DeletedTitle = %q, want %q", c.name, got, c.want)
		}
		// What import-community recovers from the title gives it back.
		creator, by, reason := parseDeletedTitle(got)
		p := deleted(creator, 0, by, 0, reason, 0)
		if again := DeletedTitle(&p); again != got {
			t.Errorf("%s: title %q parsed to %+v, which gives %q", c.name, got, p, again)
		}
	}
}

func TestMultiPacking(t *testing.T) {
	personID := model.MakePersonID(42, 1700000000)
	for _, c := range []struct {
		post model.Post
		want uint32
	}{
		{model.Post{Money: 1234}, 1234},
		{model.Post{Money: -1}, 0xffffffff},
		{model.Post{Filemode: model.FileAnonymous, CreatorID: personID, Money: 5}, 42},
		{model.Post{Filemode: model.FilePetition, Money: 5}, 0},
	} {
		if got := c.post.PackMulti(); got != c.want {
			t.Errorf("PackMulti of %+v = %#x, want %#x", c.post, got, c.want)
		}
	}
	if personID.Usernum() != 42 || personID.Firstlogin() != 1700000000 {
		t.Errorf("person ID %d: usernum %d, firstlogin %d", personID, personID.Usernum(), personID.Firstlogin())
	}
}

// fakeUsers is a UserResolver of a fixed set of accounts.
type fakeUsers map[string]struct {
	usernum    int
	firstlogin uint32
}

func (f fakeUsers) LookupUserid(userid string) (int, uint32, bool) {
	u, ok := f[userid]
	return u.usernum, u.firstlogin, ok
}

func (f fakeUsers) LookupUsernum(usernum int) (string, uint32, bool) {
	for id, u := range f {
		if u.usernum == usernum {
			return id, u.firstlogin, true
		}
	}
	return "", 0, false
}

func TestResolveUID(t *testing.T) {
	users := fakeUsers{"userA": {1, 1600000000}, "reborn": {2, 1750000000}}
	if got := ResolvePersonID(users, "userA", 0, 1728000000); got != model.MakePersonID(1, 1600000000) {
		t.Errorf("userA: %d", got)
	}
	// The account was created after the post: it is not the author.
	if got := ResolvePersonID(users, "reborn", 0, 1728000000); got != 0 {
		t.Errorf("reborn: %d, want 0", got)
	}
	if got := ResolvePersonID(users, "", 2, 1760000000); got != model.MakePersonID(2, 1750000000) {
		t.Errorf("usernum 2: %d", got)
	}
	if got := ResolvePersonID(users, "gone", 0, 1728000000); got != 0 {
		t.Errorf("gone: %d, want 0", got)
	}
	if got := ResolvePersonID(nil, "userA", 0, 1728000000); got != 0 {
		t.Errorf("without resolver: %d, want 0", got)
	}
}

type dirFixture struct {
	filename string // as in .DIR
	owner    string
	title    string
	modified uint32
	score    int16
	filemode uint16
	multi    uint32
	comments uint16
	content  string // empty: no article file
}

func (f dirFixture) record() []byte {
	rec := make([]byte, FileHeaderSize)
	copy(rec[fhdrOffFilename:], f.filename)
	binary.LittleEndian.PutUint32(rec[fhdrOffModified:], f.modified)
	binary.LittleEndian.PutUint16(rec[fhdrOffScore:], uint16(f.score))
	copy(rec[fhdrOffOwner:], f.owner)
	orig, _ := IsSafeDeletedFilename(f.filename)
	copy(rec[fhdrOffDate:], fhdrDate(stampTime(orig)))
	copy(rec[fhdrOffTitle:], big5uao.Encode(f.title))
	binary.LittleEndian.PutUint32(rec[fhdrOffMulti:], f.multi)
	binary.LittleEndian.PutUint16(rec[fhdrOffFilemode:], f.filemode)
	binary.LittleEndian.PutUint16(rec[fhdrOffComments:], f.comments)
	return rec
}

func readDirRecords(t *testing.T, path string) [][]byte {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var recs [][]byte
	for off := 0; off+FileHeaderSize <= len(data); off += FileHeaderSize {
		recs = append(recs, data[off:off+FileHeaderSize])
	}
	return recs
}

func recFilename(rec []byte) string {
	return cString(rec[fhdrOffFilename : fhdrOffFilename+fhdrLenFilename])
}

func TestSyncDir(t *testing.T) {
	tempDir := t.TempDir()
	boardDir := filepath.Join(tempDir, "boards", "SyncBoard")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatal(err)
	}

	sig := "--\n※ 發信站: 批踢踢實業坊\n"
	fixtures := []dirFixture{
		// 0: marked post with money
		{filename: "M.1728000001.A.001", owner: "userA", title: "[問卦] 第一篇", modified: 1728000100,
			filemode: 0x0002, multi: 50, score: 9,
			content: "本文一\n" + sig + "推 userB: 推 10/01 12:00\n推 userC: 推 10/01 12:01\n→ userD: 嗯 10/01 12:02\n"},
		// 1: deleted by its author in post.svc after the import
		{filename: "M.1728000002.A.002", owner: "userB", title: "第二篇", modified: 1728000200,
			content: "本文二\n" + sig + "噓 userA: 噓 10/01 12:05\n"},
		// 2: already safe-deleted by a moderator in .DIR
		{filename: ".d1728000003.A.003", owner: "-", title: "(本文已被刪除) <userC>", modified: 1728000300,
			score: -3, comments: 4},
		// 3: anonymous, in post.svc but (later) not in .DIR
		{filename: "M.1728000004.A.004", owner: "Anonymous.", title: "第四篇", modified: 1728000400,
			filemode: model.FileAnonymous, multi: 7,
			content: "本文四\n" + sig},
		// 4: not in post.svc (no article file)
		{filename: "M.1728000005.A.005", owner: "userE", title: "第五篇", modified: 1728000500},
	}
	var dirBuf []byte
	for _, f := range fixtures {
		if f.content != "" {
			if err := os.WriteFile(filepath.Join(boardDir, f.filename), big5uao.Encode(f.content), 0644); err != nil {
				t.Fatal(err)
			}
		}
		dirBuf = append(dirBuf, f.record()...)
	}
	dirPath := filepath.Join(boardDir, ".DIR")
	if err := os.WriteFile(dirPath, dirBuf, 0644); err != nil {
		t.Fatal(err)
	}

	engineDir := filepath.Join(tempDir, "engine")
	engine, err := storage.OpenEngine(storage.Config{
		DataDir:        engineDir,
		CacheDir:       filepath.Join(engineDir, "cache"),
		FlushSeconds:   2,
		FilterEncoding: "big5",
	})
	if err != nil {
		t.Fatal(err)
	}
	defer engine.Close()

	users := fakeUsers{
		"userA":    {1, 1600000000},
		"userB":    {2, 1600000000},
		"userC":    {3, 1600000000},
		"realuser": {7, 1700000000},
	}
	uidOf := func(id string) model.PersonID { return model.MakePersonID(users[id].usernum, users[id].firstlogin) }
	importStats, err := MigrateBoard(engine, ImportBoardOptions{BBSHome: tempDir, Board: "SyncBoard", IsBig5: true, Users: users})
	if err != nil {
		t.Fatalf("MigrateBoard failed: %v", err)
	}
	if importStats.PostsImported != 4 || importStats.DeletedImported != 1 {
		t.Fatalf("MigrateBoard: %+v, want 4 posts imported, 1 of them deleted", importStats)
	}

	// Metadata from .DIR is imported.
	p1, err := engine.GetPostByCommunityFile("SyncBoard", "M.1728000001.A.001")
	if err != nil {
		t.Fatal(err)
	}
	if p1.Filemode != 0x0002 || p1.Money != 50 || p1.Modified != 1728000100 || p1.CreatorID != uidOf("userA") {
		t.Errorf("post 1: filemode=%#x money=%d modified=%d personID=%d", p1.Filemode, p1.Money, p1.Modified, p1.CreatorID)
	}
	cmts, err := engine.GetComments(p1.ID, 1, 10)
	if err != nil || len(cmts) != 3 || cmts[0].CreatorID != uidOf("userB") || cmts[2].CreatorID != 0 {
		t.Errorf("post 1 comments: %+v, %v (userD is unknown)", cmts, err)
	}
	p3, err := engine.GetPostByCommunityFile("SyncBoard", "M.1728000003.A.003")
	if err != nil {
		t.Fatal(err)
	}
	if !p3.DeletedOrRemoved() || p3.CreatorName != "userC" || p3.CreatorID != uidOf("userC") || p3.NumComments != 4 || p3.Downvotes != 3 {
		t.Errorf("safe-deleted post 3 imported as %+v", p3)
	}
	p4, err := engine.GetPostByCommunityFile("SyncBoard", "M.1728000004.A.004")
	if err != nil {
		t.Fatal(err)
	}
	if p4.CreatorName != "Anonymous." || p4.CreatorID != uidOf("realuser") || p4.Money != 0 {
		t.Errorf("anonymous post 4: name=%q personID=%d money=%d", p4.CreatorName, p4.CreatorID, p4.Money)
	}

	// Changes after the import: .DIR loses post 4, post 2 is deleted by its
	// author, and post 1 gets a vote.
	original := readDirRecords(t, dirPath)
	withoutP4 := bytes.Join([][]byte{original[0], original[1], original[2], original[4]}, nil)
	if err := os.WriteFile(dirPath, withoutP4, 0644); err != nil {
		t.Fatal(err)
	}
	if _, err := engine.DeletePostByCommunityFile("SyncBoard", "M.1728000002.A.002", "userB", uidOf("userB"), ""); err != nil {
		t.Fatal(err)
	}
	if _, _, err := engine.VotePost(p1.ID, "userE", 0, model.VoteDown, time.Now().Unix(), ""); err != nil {
		t.Fatal(err)
	}

	sync := func(opts SyncDirOptions) *SyncDirStats {
		t.Helper()
		opts.BBSHome, opts.Board, opts.IsBig5 = tempDir, "SyncBoard", true
		stats, err := SyncDir(engine, opts)
		if err != nil {
			t.Fatalf("SyncDir(%+v) failed: %v", opts, err)
		}
		return stats
	}

	// --only-scores
	if stats := sync(SyncDirOptions{DryRun: true}); stats.Records != 4 || stats.Matched != 3 || stats.Updated != 2 {
		t.Errorf("only-scores dry-run: %+v, want 4 records, 3 matched, 2 updated", stats)
	}
	if got := readDirRecords(t, dirPath); !bytes.Equal(bytes.Join(got, nil), withoutP4) {
		t.Fatal("dry-run modified .DIR")
	}
	sync(SyncDirOptions{})
	recs := readDirRecords(t, dirPath)
	// post 1: 2 up (userB, userC) and 1 down (userE); 3 comments.
	if r := int16(binary.LittleEndian.Uint16(recs[0][fhdrOffScore:])); r != 1 {
		t.Errorf("post 1 score = %d, want 1", r)
	}
	if c := binary.LittleEndian.Uint16(recs[0][fhdrOffComments:]); c != 3 {
		t.Errorf("post 1 comments = %d, want 3", c)
	}
	if recFilename(recs[1]) != "M.1728000002.A.002" {
		t.Errorf("only-scores changed the file name of post 2 to %q", recFilename(recs[1]))
	}
	if stats := sync(SyncDirOptions{}); stats.Updated != 0 {
		t.Errorf("second only-scores updated %d records", stats.Updated)
	}

	// --full
	if err := engine.SetPostMeta("SyncBoard", "M.1728000001.A.001", 0x0012, 99); err != nil {
		t.Fatal(err)
	}
	if stats := sync(SyncDirOptions{Full: true}); stats.Updated != 2 {
		t.Errorf("full: %+v, want 2 updated (post 1 meta, post 2 deleted)", stats)
	}
	recs = readDirRecords(t, dirPath)
	if len(recs) != 4 {
		t.Fatalf("full changed the number of records to %d", len(recs))
	}
	if fm := binary.LittleEndian.Uint16(recs[0][fhdrOffFilemode:]); fm != 0x0012 {
		t.Errorf("post 1 filemode = %#x, want 0x12", fm)
	}
	if m := binary.LittleEndian.Uint32(recs[0][fhdrOffMulti:]); m != 99 {
		t.Errorf("post 1 money = %d, want 99", m)
	}
	// Everything else of post 1 is as mbbsd wrote it.
	want0 := append([]byte(nil), original[0]...)
	copy(want0[fhdrOffScore:], recs[0][fhdrOffScore:fhdrOffScore+2])
	copy(want0[fhdrOffMulti:], recs[0][fhdrOffMulti:fhdrOffComments+2])
	if !bytes.Equal(recs[0], want0) {
		t.Errorf("post 1 record:\n got %q\nwant %q", recs[0], want0)
	}
	// Post 2 is rewritten the way mbbsd safe-deletes it.
	if fn, owner, title := recFilename(recs[1]), cString(recs[1][fhdrOffOwner:fhdrOffOwner+fhdrLenOwner]),
		big5uao.Decode([]byte(cString(recs[1][fhdrOffTitle:fhdrOffTitle+fhdrLenTitle]))); fn != ".d1728000002.A.002" || owner != "-" || title != "(本文已被刪除) [userB]" {
		t.Errorf("deleted post 2: filename=%q owner=%q title=%q", fn, owner, title)
	}
	// The record imported from a safe-deleted one comes back unchanged.
	if !bytes.Equal(recs[2], original[2]) {
		t.Errorf("safe-deleted post 3:\n got %q\nwant %q", recs[2], original[2])
	}
	if !bytes.Equal(recs[3], original[4]) {
		t.Error("full changed the record of a post unknown to post.svc")
	}
	if stats := sync(SyncDirOptions{Full: true}); stats.Updated != 0 {
		t.Errorf("second full updated %d records", stats.Updated)
	}
	// Petitions follow the board's limits: their multi (where old petitions
	// kept limits) is cleared.
	if err := engine.SetPostMeta("SyncBoard", "M.1728000001.A.001", model.FilePetition, 0); err != nil {
		t.Fatal(err)
	}
	sync(SyncDirOptions{Full: true})
	recs = readDirRecords(t, dirPath)
	if fm, m := binary.LittleEndian.Uint16(recs[0][fhdrOffFilemode:]), binary.LittleEndian.Uint32(recs[0][fhdrOffMulti:]); fm != model.FilePetition || m != 0 {
		t.Errorf("petition post 1: filemode=%#x multi=%d, want %#x and 0", fm, m, model.FilePetition)
	}

	// --prune --add-missing
	if stats := sync(SyncDirOptions{Full: true, Prune: true, AddMissing: true, DryRun: true}); stats.Pruned != 2 || stats.Added != 1 || stats.AddedNoFile != 0 {
		t.Errorf("prune/add-missing dry-run: %+v, want 2 pruned, 1 added", stats)
	}
	if len(readDirRecords(t, dirPath)) != 4 {
		t.Fatal("dry-run changed .DIR")
	}
	sync(SyncDirOptions{Full: true, Prune: true, AddMissing: true})
	recs = readDirRecords(t, dirPath)
	var names []string
	for _, r := range recs {
		names = append(names, recFilename(r))
	}
	if len(names) != 3 || names[0] != "M.1728000001.A.001" || names[1] != "M.1728000004.A.004" || names[2] != "M.1728000005.A.005" {
		t.Errorf("after prune/add-missing: %v", names)
	}
	if !bytes.Equal(recs[1], original[3]) {
		t.Errorf("added post 4:\n got %q\nwant %q", recs[1], original[3])
	}
	baks, _ := filepath.Glob(dirPath + ".bak-*")
	if len(baks) != 1 {
		t.Errorf("backups: %v, want 1", baks)
	} else if bak := readDirRecords(t, baks[0]); len(bak) != 4 {
		t.Errorf("backup has %d records, want 4", len(bak))
	}
	if stats := sync(SyncDirOptions{Full: true, Prune: true, AddMissing: true}); stats.Updated+stats.Pruned+stats.Added != 0 {
		t.Errorf("second prune/add-missing: %+v", stats)
	}
}
