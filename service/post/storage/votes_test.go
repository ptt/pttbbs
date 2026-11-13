package storage

import (
	"testing"

	"pttbbs/post/model"
)

func TestVotesByPerson(t *testing.T) {
	e, _ := openTestEngine(t)
	p := mustCreatePost(t, e, "Board", "M.1.A.001", "body")
	alice := model.MakePersonID(1, 1600000000)
	vote := func(name string, id model.PersonID, v model.VoteType) (int, int) {
		t.Helper()
		up, down, err := e.VotePost(p.ID, name, id, v, 0, "")
		if err != nil {
			t.Fatal(err)
		}
		return up, down
	}

	// A person has one vote whatever name it is shown with.
	if up, _ := vote("alice", alice, model.VoteUp); up != 1 {
		t.Fatalf("first vote of alice: up %d", up)
	}
	if up, down := vote("nickname", alice, model.VoteDown); up != -1 || down != 1 {
		t.Fatalf("alice voting again under another name = %d, %d; want -1, 1", up, down)
	}
	// Unknown persons go by name.
	vote("ghost1", 0, model.VoteUp)
	vote("ghost2", 0, model.VoteUp)
	// A known person takes over the vote of its name as an unknown person.
	vote("bob", 0, model.VoteUp)
	if up, down := vote("bob", model.MakePersonID(2, 1600000001), model.VoteUp); up != 0 || down != 0 {
		t.Fatalf("bob voting the same as his legacy vote = %d, %d; want 0, 0", up, down)
	}
	if up, down := vote("bob", model.MakePersonID(2, 1600000001), model.VoteDown); up != -1 || down != 1 {
		t.Fatalf("bob changing his legacy vote = %d, %d; want -1, 1", up, down)
	}

	votes, err := e.GetVotes(p.ID)
	if err != nil {
		t.Fatal(err)
	}
	byName := map[string]*model.VoteRecord{}
	for _, v := range votes {
		byName[v.PersonName] = v
	}
	if len(votes) != 4 || byName["nickname"] == nil || byName["nickname"].PersonID != alice ||
		byName["nickname"].Vote != model.VoteDown || byName["bob"] == nil || byName["bob"].Vote != model.VoteDown {
		t.Fatalf("votes: %+v", byName)
	}
}
