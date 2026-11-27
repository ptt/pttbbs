package model

import (
	"bytes"
	"testing"
)

// Records written before the Encoding header was dropped must still decode.
func TestDecodeRFC822LegacyEncodingHeader(t *testing.T) {
	legacy := func(b []byte) []byte { return append([]byte("Encoding: utf-8\n"), b...) }

	c := &Comment{CreatorName: "alice", CreatorID: 7, CreatedAt: 100, IP: "1.2.3.4", LegacyType: 1, Content: "hi"}
	enc := EncodeRFC822Comment(c)
	if bytes.Contains(enc, []byte("Encoding")) {
		t.Fatalf("encoded comment still has Encoding: %q", enc)
	}
	got, err := DecodeRFC822Comment(1, 2, legacy(enc))
	if err != nil || got.CreatorName != "alice" || got.CreatorID != 7 || got.CreatedAt != 100 || got.Content != "hi" {
		t.Fatalf("legacy comment: %+v, %v", got, err)
	}

	p := &Post{CommunityID: 3, PostFile: "M.1.A.ABC", Title: "t", CreatorName: "bob", CreatedAt: 200, Content: "body"}
	meta, body := DecodeRFC822Post(legacy(EncodeRFC822Post(p)))
	if meta["PostFile"] != "M.1.A.ABC" || meta["CommunityID"] != "3" || body != "body" {
		t.Fatalf("legacy post: %v %q", meta, body)
	}

	x, err := DecodeRFC822Crosspost(legacy(EncodeRFC822Crosspost(&CrosspostRecord{SourcePostID: 9, TargetPostFile: "M.2.A.DEF"})))
	if err != nil || x.SourcePostID != 9 || x.TargetPostFile != "M.2.A.DEF" {
		t.Fatalf("legacy crosspost: %+v, %v", x, err)
	}

	cm, err := DecodeRFC822Community(legacy(EncodeRFC822Community(&Community{Name: "Test", Title: "x"})), nil)
	if err != nil || cm.Name != "Test" || cm.Title != "x" {
		t.Fatalf("legacy community: %+v, %v", cm, err)
	}
}
