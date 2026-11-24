package storage

import (
	"fmt"
	"strings"
	"sync"
	"time"

	"pttbbs/nest/model"
)

// Persons (model.Person), kept with the communities in communities.db
// (shared by all engines) in two tables, as Lemmy has them:
//
//   - person: id (a small serial, as Lemmy clients need) -> name (BBS
//     userid), display_name (BBS nickname), deleted, ...
//   - local_user: the local account behind a person, here the BBS account
//     (bbs_user: user_t, usernum and firstlogin, as model.PersonID) ->
//     person_id. Keying the account by usernum alone would not do, as
//     .PASSWDS reuses the slots of deleted accounts; when accounts get IDs
//     of their own, local_user is where they go.
//
// Anonymous persons (the nicknames of anonymous posts) and legacy persons
// (userids of accounts that cannot be told any more) have no local_user.
// A person is registered the first time it is shown.
//
// Deviation from Lemmy: posts, comments and votes keep the BBS account
// (model.PersonID, what mbbsd sends) of their creator rather than the
// person ID, so that writes need no lookup and anonymous posts keep their
// real author privately; the person ID is looked up when they are shown.

const personSchema = `
	CREATE TABLE IF NOT EXISTS person (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		name TEXT NOT NULL,
		display_name TEXT NOT NULL DEFAULT '',
		bbs_anonymous INTEGER NOT NULL DEFAULT 0,
		deleted INTEGER NOT NULL DEFAULT 0,
		published INTEGER NOT NULL,
		updated INTEGER NOT NULL DEFAULT 0
	);
	CREATE INDEX IF NOT EXISTS idx_person_name ON person(name COLLATE NOCASE);
	CREATE UNIQUE INDEX IF NOT EXISTS idx_person_anonymous ON person(display_name) WHERE bbs_anonymous = 1;

	CREATE TABLE IF NOT EXISTS local_user (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		person_id INTEGER NOT NULL UNIQUE,
		bbs_user INTEGER NOT NULL UNIQUE
	);
`

// PersonLookup tells the userid and nickname of the BBS account user, and
// whether it still exists.
type PersonLookup func(user model.PersonID) (userid, nickname string, ok bool)

// personRefresh is how often a shown person is checked against its account.
const personRefresh = 24 * time.Hour

type personTable struct {
	mu     sync.Mutex
	byID   map[int64]*model.Person
	byUser map[model.PersonID]int64
	byName map[string]int64 // lower-cased name of active persons with accounts
	legacy map[string]int64 // lower-cased name of legacy persons
	byNick map[string]int64 // nickname of anonymous persons
	lookup PersonLookup
}

func (t *personTable) init() {
	t.byID = make(map[int64]*model.Person)
	t.byUser = make(map[model.PersonID]int64)
	t.byName = make(map[string]int64)
	t.legacy = make(map[string]int64)
	t.byNick = make(map[string]int64)
}

func (t *personTable) add(p *model.Person) {
	t.byID[p.ID] = p
	switch {
	case p.Anonymous:
		t.byNick[p.DisplayName] = p.ID
	case p.User != 0:
		t.byUser[p.User] = p.ID
		if !p.Deleted {
			t.byName[communityKey(p.Name)] = p.ID
		}
	default:
		t.legacy[communityKey(p.Name)] = p.ID
	}
}

func (r *CommunityRegistry) loadPersons() error {
	t := &r.persons
	t.init()
	rows, err := r.db.Query(`SELECT p.id, p.name, p.display_name, COALESCE(u.bbs_user, 0), p.bbs_anonymous, p.deleted, p.published, p.updated
		FROM person p LEFT JOIN local_user u ON u.person_id = p.id`)
	if err != nil {
		return err
	}
	defer rows.Close()
	for rows.Next() {
		p := &model.Person{}
		var user int64
		if err := rows.Scan(&p.ID, &p.Name, &p.DisplayName, &user, &p.Anonymous, &p.Deleted, &p.Published, &p.Updated); err != nil {
			return err
		}
		p.User = model.PersonID(uint64(user))
		t.add(p)
	}
	return rows.Err()
}

// SetPersonLookup sets how persons are checked against their BBS accounts
// (for their nicknames, and whether the accounts are gone).
func (r *CommunityRegistry) SetPersonLookup(lookup PersonLookup) {
	r.persons.mu.Lock()
	defer r.persons.mu.Unlock()
	r.persons.lookup = lookup
}

// insertPerson registers p (and its local_user, if it has an account).
func (r *CommunityRegistry) insertPerson(p *model.Person) error {
	tx, err := r.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	res, err := tx.Exec(`INSERT INTO person (name, display_name, bbs_anonymous, deleted, published, updated) VALUES (?, ?, ?, ?, ?, ?)`,
		p.Name, p.DisplayName, p.Anonymous, p.Deleted, p.Published, p.Updated)
	if err != nil {
		return fmt.Errorf("register person %s failed: %w", p.Name, err)
	}
	if p.ID, err = res.LastInsertId(); err != nil {
		return err
	}
	if p.Anonymous {
		p.Name = fmt.Sprintf("anon%d", p.ID)
		if _, err := tx.Exec(`UPDATE person SET name = ? WHERE id = ?`, p.Name, p.ID); err != nil {
			return err
		}
	}
	if p.User != 0 {
		if _, err := tx.Exec(`INSERT INTO local_user (person_id, bbs_user) VALUES (?, ?)`, p.ID, int64(p.User)); err != nil {
			return fmt.Errorf("register account of person %s failed: %w", p.Name, err)
		}
	}
	return tx.Commit()
}

func (r *CommunityRegistry) savePerson(p *model.Person) error {
	_, err := r.db.Exec(`UPDATE person SET name = ?, display_name = ?, deleted = ?, updated = ? WHERE id = ?`,
		p.Name, p.DisplayName, p.Deleted, p.Updated, p.ID)
	return err
}

// deletePersonLocked marks the person deleted (its account is gone), which
// frees its name.
func (r *CommunityRegistry) deletePersonLocked(p *model.Person, now int64) error {
	p.Deleted, p.Updated = true, now
	if err := r.savePerson(p); err != nil {
		return err
	}
	if id, ok := r.persons.byName[communityKey(p.Name)]; ok && id == p.ID {
		delete(r.persons.byName, communityKey(p.Name))
	}
	return nil
}

// refreshPersonLocked checks a person with an account against it, at most
// once per personRefresh: its nickname, the case of its userid, and whether
// the account is gone.
func (r *CommunityRegistry) refreshPersonLocked(p *model.Person, now int64) {
	t := &r.persons
	if t.lookup == nil || p.User == 0 || p.Deleted || now-p.Updated < int64(personRefresh/time.Second) {
		return
	}
	userid, nick, ok := t.lookup(p.User)
	if !ok {
		_ = r.deletePersonLocked(p, now)
		return
	}
	if strings.EqualFold(userid, p.Name) {
		p.Name = userid
	}
	p.DisplayName, p.Updated = nick, now
	_ = r.savePerson(p)
}

// Person returns the person of the BBS account user, shown as name,
// registering it if it is new. With user 0, it is the legacy person named
// name (an account that cannot be told any more).
func (r *CommunityRegistry) Person(user model.PersonID, name string) (model.Person, error) {
	name = strings.TrimSpace(name)
	t := &r.persons
	t.mu.Lock()
	defer t.mu.Unlock()
	now := time.Now().Unix()

	if user == 0 {
		if name == "" {
			return model.Person{}, fmt.Errorf("person without account or name")
		}
		if id, ok := t.legacy[communityKey(name)]; ok {
			return *t.byID[id], nil
		}
		p := &model.Person{Name: name, Deleted: true, Published: now}
		if err := r.insertPerson(p); err != nil {
			return model.Person{}, err
		}
		t.add(p)
		return *p, nil
	}

	if id, ok := t.byUser[user]; ok {
		p := t.byID[id]
		r.refreshPersonLocked(p, now)
		return *p, nil
	}
	p := &model.Person{User: user, Name: name, Published: now}
	if t.lookup != nil {
		userid, nick, ok := t.lookup(user)
		if ok && userid != "" {
			p.Name, p.DisplayName = userid, nick
		}
		p.Updated = now
	}
	if p.Name == "" {
		return model.Person{}, fmt.Errorf("person %d without a name", uint64(user))
	}
	// The userid was taken again: the account of the old person is gone.
	if id, ok := t.byName[communityKey(p.Name)]; ok {
		if err := r.deletePersonLocked(t.byID[id], now); err != nil {
			return model.Person{}, err
		}
	}
	if err := r.insertPerson(p); err != nil {
		return model.Person{}, err
	}
	t.add(p)
	return *p, nil
}

// AnonymousPerson returns the person shown for anonymous posts by
// nickname, registering it (as anon<id>) if it is new. It is not linked to
// any account.
func (r *CommunityRegistry) AnonymousPerson(nickname string) (model.Person, error) {
	nickname = strings.TrimSpace(nickname)
	t := &r.persons
	t.mu.Lock()
	defer t.mu.Unlock()
	if id, ok := t.byNick[nickname]; ok {
		return *t.byID[id], nil
	}
	p := &model.Person{DisplayName: nickname, Anonymous: true, Published: time.Now().Unix()}
	if err := r.insertPerson(p); err != nil {
		return model.Person{}, err
	}
	t.add(p)
	return *p, nil
}

// GetPerson returns the person with the given (Lemmy) person ID.
func (r *CommunityRegistry) GetPerson(id int64) (model.Person, bool) {
	t := &r.persons
	t.mu.Lock()
	defer t.mu.Unlock()
	if p, ok := t.byID[id]; ok {
		return *p, true
	}
	return model.Person{}, false
}
