package model

import "testing"

// Pairs computed with fn2aidu/aidu2aidc/aidc2aidu/aidu2fn of common/bbs/aids.c.
var aidPairs = []struct{ file, aid string }{
	{"M.1234567890.A.ABC", "19bWBIgy"},
	{"M.1700000000.A.001", "1bK_4001"},
	{"G.1111111111.A.FFF", "52EZN7__"},
	{"M.1764219999.A.5F2", "1f9znVNo"},
	{"M.1120582370.A.1EA", "12ohhY7g"},
}

func TestAIDToPostFile(t *testing.T) {
	for _, p := range aidPairs {
		if got, ok := AIDToPostFile(p.aid); !ok || got != p.file {
			t.Errorf("AIDToPostFile(%q) = %q, %v; want %q", p.aid, got, ok, p.file)
		}
		if got, ok := PostFileToAID(p.file); !ok || got != p.aid {
			t.Errorf("PostFileToAID(%q) = %q, %v; want %q", p.file, got, ok, p.aid)
		}
	}
	// No hex suffix: aidu2fn gives it back as .A.000.
	if got, _ := PostFileToAID("M.946684800.A"); got != "0uRKE000" {
		t.Errorf("PostFileToAID(M.946684800.A) = %q", got)
	}
	if got, _ := AIDToPostFile("0uRKE000"); got != "M.946684800.A.000" {
		t.Errorf("AIDToPostFile(0uRKE000) = %q", got)
	}
	for _, bad := range []string{"", "1234567", "123456789", "1bK_40#1", "00000000"} {
		if got, ok := AIDToPostFile(bad); ok {
			t.Errorf("AIDToPostFile(%q) = %q, want failure", bad, got)
		}
	}
}
