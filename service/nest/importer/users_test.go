package importer

import (
	"strings"
	"testing"

	"pttbbs/nest/model"
)

// foldUsers looks up user IDs case-insensitively, as the SHM does.
type foldUsers fakeUsers

func (f foldUsers) LookupUserid(userid string) (int, uint32, bool) {
	return fakeUsers(f).LookupUserid(strings.ToLower(userid))
}

func (f foldUsers) LookupUsernum(usernum int) (string, uint32, bool) {
	return fakeUsers(f).LookupUsernum(usernum)
}

func TestConvertParsedCommentsCaseChangedID(t *testing.T) {
	users := foldUsers{"ansonku625": {usernum: 7, firstlogin: 1000}}
	_, votes, up, down := ConvertParsedComments([]*ParsedComment{
		{CreatorName: "ansonku625", LegacyType: model.LegacyTypeBoo, CTime: 2000},
		{CreatorName: "ANSONKU625", LegacyType: model.LegacyTypePush, CTime: 2001},
		{CreatorName: "guest", LegacyType: model.LegacyTypePush, CTime: 2002},
		{CreatorName: "Guest", LegacyType: model.LegacyTypePush, CTime: 2003},
	}, users)
	// Unresolved names stay apart, as their vote keys do.
	if up != 3 || down != 0 || len(votes) != 3 {
		t.Fatalf("got %d up, %d down, %d votes; want 3, 0, 3", up, down, len(votes))
	}
	if v := votes[0]; v.PersonName != "ANSONKU625" || v.Vote != model.VoteUp || v.CreatedAt != 2001 {
		t.Errorf("vote of the case-changed ID = %+v; want the last one", v)
	}
}
