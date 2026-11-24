package main

import (
	"encoding/binary"
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

// import-board --all imports the boards of .BRD that have posts of their
// own: not empty slots, links (BRD_SYMBOLIC) or boards without .DIR.
func TestBoardsToImport(t *testing.T) {
	bbsHome := t.TempDir()
	record := func(name string, attr uint32) []byte {
		b := make([]byte, brdRecordSize)
		copy(b, name)
		binary.LittleEndian.PutUint32(b[brdAttrOff:], attr)
		return b
	}
	var brd []byte
	for _, r := range [][]byte{
		record("Alpha", 0),
		make([]byte, brdRecordSize), // deleted slot
		record("Link", brdSymbolic),
		record("Group", 0x8), // no .DIR
		record("Beta", 0),
	} {
		brd = append(brd, r...)
	}
	brdPath := filepath.Join(bbsHome, ".BRD")
	if err := os.WriteFile(brdPath, brd, 0644); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"Alpha", "Beta", "Link"} {
		dir := filepath.Join(bbsHome, "boards", name[:1], name)
		if err := os.MkdirAll(dir, 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(dir, ".DIR"), nil, 0644); err != nil {
			t.Fatal(err)
		}
	}
	boards, skipped, err := boardsToImport(bbsHome, brdPath)
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(boards, []string{"Alpha", "Beta"}) || !reflect.DeepEqual(skipped, []string{"Group"}) {
		t.Errorf("boardsToImport = %v (skipped %v)", boards, skipped)
	}
}
