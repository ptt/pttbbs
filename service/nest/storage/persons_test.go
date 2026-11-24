package storage

import (
	"fmt"
	"testing"

	"pttbbs/nest/model"
)

func TestPersons(t *testing.T) {
	dir := t.TempDir()
	reg, err := OpenCommunityRegistry(dir)
	if err != nil {
		t.Fatal(err)
	}
	alice := model.MakePersonID(5, 1600000000)
	newAlice := model.MakePersonID(5, 1700000000) // the slot and the userid taken again

	// Without a lookup, a person goes by the name it is shown with.
	a, err := reg.Person(alice, "alice")
	if err != nil || a.Name != "alice" || a.User != alice || a.Deleted {
		t.Fatalf("Person(alice) = %+v, %v", a, err)
	}
	if again, _ := reg.Person(alice, "whatever"); again.ID != a.ID {
		t.Fatalf("the same account got another person: %+v", again)
	}

	accounts := map[model.PersonID][2]string{newAlice: {"Alice", "new nick"}}
	reg.SetPersonLookup(func(user model.PersonID) (string, string, bool) {
		acc, ok := accounts[user]
		return acc[0], acc[1], ok
	})
	// The old account is gone (checked as it was never checked before).
	if a, _ = reg.Person(alice, "alice"); !a.Deleted {
		t.Fatalf("person of a gone account not deleted: %+v", a)
	}
	b, err := reg.Person(newAlice, "alice")
	if err != nil || b.ID == a.ID || b.Name != "Alice" || b.DisplayName != "new nick" || b.Deleted {
		t.Fatalf("Person(new alice) = %+v, %v", b, err)
	}

	// Legacy and anonymous persons have no account.
	legacy, err := reg.Person(0, "bob")
	if err != nil || !legacy.Deleted || legacy.User != 0 {
		t.Fatalf("Person(0, bob) = %+v, %v", legacy, err)
	}
	anon, err := reg.AnonymousPerson("路人")
	if err != nil || anon.Name != fmt.Sprintf("anon%d", anon.ID) || anon.DisplayName != "路人" || anon.User != 0 {
		t.Fatalf("AnonymousPerson = %+v, %v", anon, err)
	}
	if again, _ := reg.AnonymousPerson("路人"); again.ID != anon.ID {
		t.Fatalf("the same nickname got another person")
	}
	reg.Close()

	// Everything is kept, the accounts (local_user) included.
	if reg, err = OpenCommunityRegistry(dir); err != nil {
		t.Fatal(err)
	}
	defer reg.Close()
	for _, want := range []model.Person{a, b, legacy, anon} {
		got, ok := reg.GetPerson(want.ID)
		if !ok || got.Name != want.Name || got.User != want.User || got.Deleted != want.Deleted || got.Anonymous != want.Anonymous {
			t.Errorf("person %d after reopening = %+v, want %+v", want.ID, got, want)
		}
	}
	if got, _ := reg.Person(newAlice, ""); got.ID != b.ID {
		t.Fatalf("account not mapped to its person after reopening: %+v", got)
	}
}
