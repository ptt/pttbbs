package storage

import (
	"testing"

	"pttbbs/nest/model"
)

// importPost imports one post (with its cplog records and origin) the way
// the importer does.
func importPost(t *testing.T, st Storage, community, file string, ctime int64, cps []*model.CrosspostRecord, origin *model.CrosspostOrigin) {
	t.Helper()
	if err := st.ImportPostBatch([]*ImportedPostData{{
		Post:       &model.Post{Community: community, PostFile: file, Title: "t", CreatorName: "userA", CreatedAt: ctime, Content: "body\n"},
		Crossposts: cps,
		Origin:     origin,
	}}, ""); err != nil {
		t.Fatalf("ImportPostBatch %s/%s: %v", community, file, err)
	}
}

func cplog(target string, at int64) *model.CrosspostRecord {
	return &model.CrosspostRecord{TargetCommunity: target, TargetPostFile: "-", CreatorName: "userA", CreatedAt: at}
}

func crosspostsOf(t *testing.T, st Storage, community, file string) []*model.CrosspostRecord {
	t.Helper()
	cps, err := st.GetCrossposts(community, file)
	if err != nil {
		t.Fatalf("GetCrossposts %s/%s: %v", community, file, err)
	}
	return cps
}

func numCrossposts(t *testing.T, st Storage, community, file string) int {
	t.Helper()
	p, err := st.GetPostByCommunityFile(community, file)
	if err != nil {
		t.Fatal(err)
	}
	return p.NumCrossposts
}

// A copy header completes the nearest cplog record of the same crosspost
// (target file "-", time to the minute) rather than adding another.
func TestCopyCompletesCplogRecord(t *testing.T) {
	e, _ := openTestEngine(t)
	const t0 = int64(1700000000)
	importPost(t, e, "Src", "M.1700000000.A.001", t0, []*model.CrosspostRecord{
		cplog("Dst", t0+3600-30),
		cplog("Dst", t0+3*86400), // another, days later
		cplog("Other", t0+3600),
	}, nil)
	importPost(t, e, "Dst", "M.1700003600.A.002", t0+3600, nil, &model.CrosspostOrigin{
		SourceCommunity: "Src", SourcePostFile: "M.1700000000.A.001", CreatorName: "userA", CreatedAt: t0 + 3600,
	})

	check := func(when string) {
		t.Helper()
		cps := crosspostsOf(t, e, "Src", "M.1700000000.A.001")
		if len(cps) != 3 {
			t.Fatalf("%s: %d records, want 3: %+v", when, len(cps), cps)
		}
		var exact int
		for _, cp := range cps {
			if cp.TargetPostFile == "M.1700003600.A.002" {
				exact++
				if cp.TargetCommunity != "Dst" || cp.CreatedAt != t0+3600 || cp.CreatorName != "userA" {
					t.Errorf("%s: completed record %+v", when, cp)
				}
			} else if cp.TargetPostFile != "-" {
				t.Errorf("%s: record %+v", when, cp)
			}
		}
		if exact != 1 {
			t.Errorf("%s: %d records of the copy, want 1", when, exact)
		}
		if n := numCrossposts(t, e, "Src", "M.1700000000.A.001"); n != 3 {
			t.Errorf("%s: num_crossposts %d, want 3", when, n)
		}
		if src, rec, err := e.GetOriginPost("Dst", "M.1700003600.A.002"); err != nil || src == nil || src.PostFile != "M.1700000000.A.001" || rec.CreatedAt != t0+3600 {
			t.Errorf("%s: GetOriginPost = %+v, %+v, %v", when, src, rec, err)
		}
	}
	check("imported")

	// Importing either again changes nothing.
	importPost(t, e, "Src", "M.1700000000.A.001", t0, []*model.CrosspostRecord{
		cplog("Dst", t0+3600-30), cplog("Dst", t0+3*86400), cplog("Other", t0+3600),
	}, nil)
	importPost(t, e, "Dst", "M.1700003600.A.002", t0+3600, nil, &model.CrosspostOrigin{
		SourceCommunity: "Src", SourcePostFile: "M.1700000000.A.001", CreatorName: "userA", CreatedAt: t0 + 3600,
	})
	check("imported again")

	if _, _, err := e.RebuildSQLiteFromPebble(); err != nil {
		t.Fatal(err)
	}
	check("rebuilt")

	// A live crosspost takes a free sequence number: no record is lost.
	if _, _, err := e.AddCrosspost("Src", "M.1700000000.A.001", "Live", "M.1700009999.A.003", "userB", 0, t0+9999); err != nil {
		t.Fatal(err)
	}
	if cps := crosspostsOf(t, e, "Src", "M.1700000000.A.001"); len(cps) != 4 {
		t.Fatalf("after AddCrosspost: %d records, want 4", len(cps))
	}
	if _, _, err := e.RebuildSQLiteFromPebble(); err != nil {
		t.Fatal(err)
	}
	if cps := crosspostsOf(t, e, "Src", "M.1700000000.A.001"); len(cps) != 4 {
		t.Fatalf("after AddCrosspost and rebuild: %d records, want 4", len(cps))
	}
}

// A copy whose source is not there stays pending, also across a rebuild,
// and is linked once the source is (in another engine).
func TestCopyPendingAcrossEngines(t *testing.T) {
	s, _ := openTestSharded(t, 2, map[string]int{"Src": 0, "Dst": 1})
	const t0 = int64(1700000000)
	origin := &model.CrosspostOrigin{SourceCommunity: "src", SourcePostFile: "M.1700000000.A.000", CreatorName: "userA", CreatedAt: t0 + 60}
	importPost(t, s, "Dst", "M.1700000060.A.002", t0+60, nil, origin)

	st, err := s.LinkCrossposts()
	if err != nil || st.Origins != 1 || st.Pending != 1 {
		t.Fatalf("LinkCrossposts before the source = %+v, %v", st, err)
	}
	if _, _, err := s.RebuildSQLiteFromPebble(); err != nil {
		t.Fatal(err)
	}
	if st, _ := s.LinkCrossposts(); st.Pending != 1 {
		t.Fatalf("pending origin lost by the rebuild: %+v", st)
	}

	// Created live (not imported, so not linked yet); the article ID gave
	// ".A.000" for this file without a hex suffix.
	src := mustCreatePost(t, s, "Src", "M.1700000000.A", "original")
	st, err = s.LinkCrossposts()
	if err != nil || st.Added != 1 || st.Pending != 0 {
		t.Fatalf("LinkCrossposts = %+v, %v", st, err)
	}
	cps := crosspostsOf(t, s, "Src", src.PostFile)
	if len(cps) != 1 || cps[0].TargetCommunity != "Dst" || cps[0].TargetPostFile != "M.1700000060.A.002" || cps[0].CreatedAt != t0+60 {
		t.Fatalf("records of the source: %+v", cps)
	}
	if n := numCrossposts(t, s, "Src", src.PostFile); n != 1 {
		t.Fatalf("num_crossposts %d, want 1", n)
	}
	if st, _ := s.LinkCrossposts(); st.Linked != 1 || st.Added != 0 {
		t.Fatalf("LinkCrossposts again = %+v", st)
	}

	// Purging the copy forgets its origin.
	if err := s.PurgePostByCommunityFile("Dst", "M.1700000060.A.002"); err != nil {
		t.Fatal(err)
	}
	if st, _ := s.LinkCrossposts(); st.Origins != 0 {
		t.Fatalf("origin of a purged copy kept: %+v", st)
	}
}

// Importing a source links the copies imported before it, whichever
// engine they are in; clearing and importing the source again (new IDs)
// links them again.
func TestSourceImportedAfterCopy(t *testing.T) {
	s, _ := openTestSharded(t, 2, map[string]int{"Src": 0, "Dst": 1})
	const t0 = int64(1700000000)
	importPost(t, s, "Dst", "M.1700000600.A.002", t0+600, nil, &model.CrosspostOrigin{
		SourceCommunity: "Src", SourcePostFile: "M.1700000000.A.001", CreatorName: "userA", CreatedAt: t0 + 600,
	})
	for round := 0; round < 2; round++ {
		if round == 1 {
			if _, _, err := s.ClearCommunity("Src"); err != nil {
				t.Fatal(err)
			}
		}
		importPost(t, s, "Src", "M.1700000000.A.001", t0, []*model.CrosspostRecord{cplog("Dst", t0+600-50)}, nil)
		cps := crosspostsOf(t, s, "Src", "M.1700000000.A.001")
		if len(cps) != 1 || cps[0].TargetPostFile != "M.1700000600.A.002" || cps[0].CreatedAt != t0+600 {
			t.Fatalf("round %d: records of the source: %+v", round, cps)
		}
		if n := numCrossposts(t, s, "Src", "M.1700000000.A.001"); n != 1 {
			t.Fatalf("round %d: num_crossposts %d, want 1", round, n)
		}
	}
}
