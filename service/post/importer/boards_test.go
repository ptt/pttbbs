package importer

import (
	"encoding/binary"
	"os"
	"path/filepath"
	"testing"

	"pttbbs/big5uao"
	"pttbbs/post/model"
	"pttbbs/post/storage"
)

// boardRecord builds a boardheader_t record.
func boardRecord(name, class, desc string, attr uint32, gid int32, bms ...[2]uint32) []byte {
	b := make([]byte, BoardHeaderSize)
	copy(b[bhBrdname:bhBrdname+12], name)
	copy(b[bhBclass:bhBclass+4], big5uao.Encode(class))
	copy(b[bhDesc:bhDesc+41], big5uao.Encode(desc))
	copy(b[bhModeratorNotes:bhModeratorNotes+38], big5uao.Encode("(徵求中)"))
	le := binary.LittleEndian
	le.PutUint32(b[bhBrdattr:], attr)
	b[bhPetitionLimitLogins] = 10
	b[bhPetitionLimitBadpost] = 1
	b[bhFastcommentPause] = 3
	b[bhPostLimitLogins] = 20
	b[bhPostLimitBadpost] = 2
	le.PutUint32(b[bhLevel:], 0x400)
	le.PutUint32(b[bhGid:], uint32(gid))
	for i, bm := range bms {
		le.PutUint32(b[bhModerators+8*i:], bm[0])
		le.PutUint32(b[bhModerators+8*i+4:], bm[1])
	}
	return b
}

func TestImportBoards(t *testing.T) {
	tempDir := t.TempDir()
	var brd []byte
	brd = append(brd, boardRecord("LifeGroup", "生活", "生活娛樂館", model.BrdGroupBoard, 0)...)
	brd = append(brd, boardRecord("Gossiping", "綜合", "◎[八卦] 八卦板", model.BrdOver18|model.BrdNoDownvote, 1,
		[2]uint32{1, 1600000000}, [2]uint32{2, 1700000000}, [2]uint32{9, 1700000000})...)
	brd = append(brd, make([]byte, BoardHeaderSize)...) // a deleted board
	brd = append(brd, boardRecord("GossipLink", "綜合", "link", model.BrdSymbolic, 1)...)
	if err := os.WriteFile(filepath.Join(tempDir, ".BRD"), brd, 0644); err != nil {
		t.Fatal(err)
	}

	engineDir := filepath.Join(tempDir, "engine")
	engine, err := storage.OpenEngine(storage.Config{DataDir: engineDir, CacheDir: filepath.Join(engineDir, "cache")})
	if err != nil {
		t.Fatal(err)
	}
	defer engine.Close()

	// userB took slot 2 after the moderator there was deleted: no name.
	users := fakeUsers{"sysop": {1, 1600000000}, "userB": {2, 1750000000}}
	opts := ImportBoardsOptions{BBSHome: tempDir, IsBig5: true, Users: users, DryRun: true}
	stats, err := ImportBoards(engine, opts)
	if err != nil || stats.Records != 4 || stats.Upserted != 2 || stats.Skipped != 2 {
		t.Fatalf("dry run: %+v, %v", stats, err)
	}
	if len(engine.Communities().List()) != 0 {
		t.Fatalf("dry run upserted communities")
	}

	opts.DryRun = false
	if _, err := ImportBoards(engine, opts); err != nil {
		t.Fatal(err)
	}
	c, ok := engine.Communities().GetByName("gossiping")
	if !ok {
		t.Fatalf("Gossiping not imported")
	}
	if c.Name != "Gossiping" || c.Title != "◎[八卦] 八卦板" || c.Category != "綜合" || c.ModeratorNotes != "(徵求中)" ||
		c.Flags != model.BrdOver18|model.BrdNoDownvote || !c.NSFW || !c.DownvotesDisabled || c.BID != 2 ||
		c.GroupBID != 1 || c.Group != "LifeGroup" || c.Level != 0x400 || c.PostLimitLogins != 20 ||
		c.PostLimitBadpost != 2 || c.PetitionLimitLogins != 10 || c.PetitionLimitBadpost != 1 || c.FastCommentPause != 3 {
		t.Fatalf("imported community: %+v", c)
	}
	if len(c.Moderators) != 3 || c.Moderators[0].PersonName != "sysop" || c.Moderators[0].PersonID != model.MakePersonID(1, 1600000000) ||
		c.Moderators[1].PersonName != "" || c.Moderators[2].PersonID != model.MakePersonID(9, 1700000000) {
		t.Fatalf("moderators: %+v", c.Moderators)
	}
	if g, ok := engine.Communities().GetByName("LifeGroup"); !ok || !g.IsGroup {
		t.Fatalf("group board: %+v", g)
	}
	if _, ok := engine.Communities().GetByName("GossipLink"); ok {
		t.Fatalf("a link was imported as a community")
	}

	// A board renamed in .BRD without post.svc fails alone.
	copy(brd[BoardHeaderSize:BoardHeaderSize+13], append([]byte("Gossip2"), make([]byte, 6)...))
	if err := os.WriteFile(filepath.Join(tempDir, ".BRD"), brd, 0644); err != nil {
		t.Fatal(err)
	}
	stats, err = ImportBoards(engine, opts)
	if err != nil || stats.Upserted != 1 || stats.Failed != 1 || len(stats.Errors) != 1 {
		t.Fatalf("import with a renamed board: %+v, %v", stats, err)
	}
	if _, ok := engine.Communities().GetByName("Gossip2"); ok {
		t.Fatalf("the renamed board became a second community")
	}
}
