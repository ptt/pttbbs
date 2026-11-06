package model

import (
	"reflect"
	"strings"
	"testing"
)

func TestCommunityRFC822RoundTrip(t *testing.T) {
	c := &Community{
		ID:                   7,
		Name:                 "Gossiping",
		Title:                "[八卦] 綜合\n八卦",
		Description:          "long\ndescription",
		Category:             "綜合",
		Flags:                BrdOver18 | BrdHide | BrdRestrictedPost | BrdNoDownvote,
		BID:                  12,
		GroupBID:             3,
		Group:                "LifeGroup",
		Level:                0x10,
		PostLimitLogins:      100,
		PostLimitBadpost:     2,
		PetitionLimitLogins:  50,
		PetitionLimitBadpost: 1,
		FastCommentPause:     5,
		ModeratorNotes:       "(徵求中)",
		Published:            1700000000,
		Updated:              1700000100,
		Deleted:              true,
		DeletedAt:            1700000200,
		Moderators: []CommunityModerator{
			{PersonID: MakePersonID(1, 1600000000), PersonName: "sysop"},
			{PersonID: MakePersonID(2, 1600000000)},
		},
	}
	c.Derive()
	got, err := DecodeRFC822Community(EncodeRFC822Community(c), nil)
	if err != nil {
		t.Fatal(err)
	}
	want := *c
	want.Title = "[八卦] 綜合 八卦" // headers are one line
	if !reflect.DeepEqual(got, &want) {
		t.Fatalf("round trip:\n got %+v\nwant %+v", got, &want)
	}
	if !got.NSFW || !got.Hidden || !got.PostingRestrictedToMembers || !got.DownvotesDisabled || got.CommentsDisabled || got.IsGroup {
		t.Fatalf("derived flags: %+v", got)
	}
}

func TestDecodeRFC822CommunityDecodesText(t *testing.T) {
	payload := "Name: Test\nTitle: \xb4\xfa\xb8\xd5\nCategory: X\nModerator: 4294967297 sysop\n"
	c, err := DecodeRFC822Community([]byte(payload), func(s string) string { return strings.ToUpper(s) })
	if err != nil {
		t.Fatal(err)
	}
	if c.Name != "Test" || c.Title != strings.ToUpper("\xb4\xfa\xb8\xd5") || len(c.Moderators) != 1 || c.Moderators[0].PersonID != 4294967297 || c.Moderators[0].PersonName != "sysop" {
		t.Fatalf("decoded: %+v", c)
	}
	if _, err := DecodeRFC822Community([]byte("Title: x\n"), nil); err == nil {
		t.Fatalf("decoded a community without a name")
	}
	if _, err := DecodeRFC822Community([]byte("Name: x\nModerator: junk\n"), nil); err == nil {
		t.Fatalf("decoded an invalid moderator")
	}
}
