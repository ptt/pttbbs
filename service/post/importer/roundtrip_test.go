package importer

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"

	"pttbbs/post/storage"
)

// Everything in a board's .DIR survives in post.svc: importing a .DIR and
// rebuilding it from post.svc alone gives back the same records (the pins,
// marks, digest, locks, money and anonymous authors included).
func TestDirRoundTrip(t *testing.T) {
	tempDir := t.TempDir()
	boardDir := filepath.Join(tempDir, "boards", "R", "RoundTrip")
	if err := os.MkdirAll(boardDir, 0755); err != nil {
		t.Fatal(err)
	}
	const body = "作者: userA (A)\n看板: RoundTrip\n標題: t\n時間: Thu Oct  3 00:00:00 2024\n\nbody\n"
	fixtures := []dirFixture{
		{filename: "M.1728000001.A.001", owner: "userA", title: "money", modified: 1728000011, multi: 30},
		{filename: "M.1728000002.A.002", owner: "userA", title: "pinned and marked", modified: 1728000012, filemode: 0x0008 | 0x0002},
		{filename: "M.1728000003.A.003", owner: "userA", title: "digest", modified: 1728000013, filemode: 0x0004},
		{filename: "M.1728000004.A.004", owner: "userA", title: "solved", modified: 1728000014, filemode: 0x0010},
		{filename: "M.1728000005.A.005", owner: "userA", title: "suspicious", modified: 1728000015, filemode: 0x0100},
		{filename: "L.1728000006.A.006", owner: "userA", title: "locked", modified: 1728000016},
		{filename: "M.1728000007.A.007", owner: "nick.", title: "anonymous", modified: 1728000017, filemode: 0x0080, multi: 42},
	}
	var dir []byte
	for _, f := range fixtures {
		dir = append(dir, f.record()...)
		file := f.filename
		if file[0] == 'L' {
			file = "X" + file[1:]
		}
		if err := os.WriteFile(filepath.Join(boardDir, file), []byte(body), 0644); err != nil {
			t.Fatal(err)
		}
	}
	dirPath := filepath.Join(boardDir, ".DIR")
	if err := os.WriteFile(dirPath, dir, 0644); err != nil {
		t.Fatal(err)
	}

	engine, err := storage.OpenEngine(storage.Config{
		BBSHome:        tempDir,
		DataDir:        filepath.Join(tempDir, "engine"),
		CacheDir:       "boards",
		FilterEncoding: "big5",
	})
	if err != nil {
		t.Fatal(err)
	}
	defer engine.Close()
	// No user resolver: the authors' accounts cannot be told.
	if _, err := ImportBoard(engine, ImportBoardOptions{BBSHome: tempDir, Board: "RoundTrip", Overwrite: true, IsBig5: true}); err != nil {
		t.Fatal(err)
	}

	if err := os.Remove(dirPath); err != nil {
		t.Fatal(err)
	}
	if _, err := SyncDir(engine, SyncDirOptions{BBSHome: tempDir, Board: "RoundTrip", IsBig5: true, Rebuild: true}); err != nil {
		t.Fatal(err)
	}
	recs := readDirRecords(t, dirPath)
	if len(recs) != len(fixtures) {
		t.Fatalf("rebuilt .DIR has %d records, want %d", len(recs), len(fixtures))
	}
	for i, f := range fixtures {
		want, got := f.record(), recs[i]
		// The comment counts come from the comments post.svc has.
		copy(want[fhdrOffComments:], got[fhdrOffComments:fhdrOffComments+2])
		if !bytes.Equal(want, got) {
			wh, _ := UnpackFileHeader(want)
			gh, _ := UnpackFileHeader(got)
			t.Errorf("record %d:\n got %+v\nwant %+v", i, gh, wh)
		}
	}
}
