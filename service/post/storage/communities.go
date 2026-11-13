package storage

import (
	"database/sql"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"pttbbs/post/model"
)

// CommunitiesDBName is the file name of the community registry, kept in the
// base data directory (the DataDir of a single engine, or the DataDir of the
// first engine when sharded).
const CommunitiesDBName = "communities.db"

var (
	// ErrCommunityNotFound is returned when a name or ID is not registered.
	ErrCommunityNotFound = errors.New("community not found")
	// ErrCommunityExists is returned when a name is used by another
	// active community.
	ErrCommunityExists = errors.New("community name already in use")
)

// CommunityRegistry keeps the communities (model.Community) and their
// moderators, mirrored from .BRD by mbbsd, and maps their names to IDs. It
// is shared by all shard engines and keeps a full copy of the tables in
// memory.
//
// A soft-deleted community keeps its ID and name (so its posts can still
// be found and purged) but its name is free to be used by a new community.
type CommunityRegistry struct {
	db  *sql.DB
	dir string // where communities.db is

	mu     sync.RWMutex
	byID   map[int64]*model.Community
	byName map[string]int64 // lower-cased name of active communities

	// assignShard chooses the shard of a new community; nil means 0.
	assignShard ShardAssigner
}

func communityKey(name string) string {
	return strings.ToLower(strings.TrimSpace(name))
}

const communitySchema = `
	CREATE TABLE IF NOT EXISTS community (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		name TEXT NOT NULL,
		title TEXT NOT NULL DEFAULT '',
		description TEXT NOT NULL DEFAULT '',
		category TEXT NOT NULL DEFAULT '',
		flags INTEGER NOT NULL DEFAULT 0,
		bid INTEGER NOT NULL DEFAULT 0,
		group_bid INTEGER NOT NULL DEFAULT 0,
		group_name TEXT NOT NULL DEFAULT '',
		level INTEGER NOT NULL DEFAULT 0,
		post_limit_logins INTEGER NOT NULL DEFAULT 0,
		post_limit_badpost INTEGER NOT NULL DEFAULT 0,
		petition_limit_logins INTEGER NOT NULL DEFAULT 0,
		petition_limit_badpost INTEGER NOT NULL DEFAULT 0,
		fastcomment_pause INTEGER NOT NULL DEFAULT 0,
		moderator_notes TEXT NOT NULL DEFAULT '',
		removed INTEGER NOT NULL DEFAULT 0,
		shard INTEGER NOT NULL DEFAULT 0,
		published INTEGER NOT NULL,
		updated INTEGER NOT NULL DEFAULT 0,
		deleted_at INTEGER NOT NULL DEFAULT 0
	);
	CREATE UNIQUE INDEX IF NOT EXISTS idx_community_active_name
		ON community(name COLLATE NOCASE) WHERE deleted_at = 0;

	CREATE TABLE IF NOT EXISTS community_moderator (
		community_id INTEGER NOT NULL,
		person_id INTEGER NOT NULL,
		person_name TEXT NOT NULL DEFAULT '',
		published INTEGER NOT NULL,
		PRIMARY KEY (community_id, person_id)
	);
`

const communityColumns = `id, name, title, description, category, flags, bid, group_bid, group_name,
	level, post_limit_logins, post_limit_badpost, petition_limit_logins, petition_limit_badpost,
	fastcomment_pause, moderator_notes, removed, shard, published, updated, deleted_at`

// OpenCommunityRegistry opens (or creates) the registry in dataDir.
func OpenCommunityRegistry(dataDir string) (*CommunityRegistry, error) {
	if err := os.MkdirAll(dataDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create data dir: %w", err)
	}
	path := filepath.Join(dataDir, CommunitiesDBName)
	db, err := sql.Open("sqlite", path+"?_pragma=journal_mode(WAL)&_pragma=busy_timeout(10000)&_pragma=synchronous(NORMAL)")
	if err != nil {
		return nil, fmt.Errorf("open community registry failed: %w", err)
	}
	// All writes go through r.mu; a single connection avoids SQLITE_BUSY.
	db.SetMaxOpenConns(1)

	if _, err := db.Exec(communitySchema); err != nil {
		db.Close()
		return nil, fmt.Errorf("init community registry schema failed: %w", err)
	}

	r := &CommunityRegistry{
		db:     db,
		dir:    dataDir,
		byID:   make(map[int64]*model.Community),
		byName: make(map[string]int64),
	}
	if err := r.load(); err != nil {
		db.Close()
		return nil, fmt.Errorf("load community registry failed: %w", err)
	}
	return r, nil
}

func (r *CommunityRegistry) load() error {
	rows, err := r.db.Query(`SELECT ` + communityColumns + ` FROM community`)
	if err != nil {
		return err
	}
	defer rows.Close()
	for rows.Next() {
		c := &model.Community{}
		var removed int
		if err := rows.Scan(&c.ID, &c.Name, &c.Title, &c.Description, &c.Category, &c.Flags, &c.BID, &c.GroupBID, &c.Group,
			&c.Level, &c.PostLimitLogins, &c.PostLimitBadpost, &c.PetitionLimitLogins, &c.PetitionLimitBadpost,
			&c.FastCommentPause, &c.ModeratorNotes, &removed, &c.Shard, &c.Published, &c.Updated, &c.DeletedAt); err != nil {
			return err
		}
		c.Removed = removed != 0
		c.Deleted = c.DeletedAt != 0
		c.Derive()
		r.byID[c.ID] = c
		if !c.Deleted {
			r.byName[communityKey(c.Name)] = c.ID
		}
	}
	if err := rows.Err(); err != nil {
		return err
	}

	mrows, err := r.db.Query(`SELECT community_id, person_id, person_name, published FROM community_moderator ORDER BY community_id, person_id`)
	if err != nil {
		return err
	}
	defer mrows.Close()
	for mrows.Next() {
		var m model.CommunityModerator
		var pid int64
		if err := mrows.Scan(&m.CommunityID, &pid, &m.PersonName, &m.Published); err != nil {
			return err
		}
		m.PersonID = model.PersonID(uint64(pid))
		if c, ok := r.byID[m.CommunityID]; ok {
			c.Moderators = append(c.Moderators, m)
		}
	}
	for _, c := range r.byID {
		model.SortModerators(c.Moderators)
	}
	return mrows.Err()
}

// Close closes the registry database.
func (r *CommunityRegistry) Close() error {
	return r.db.Close()
}

// Dir returns the directory that holds communities.db.
func (r *CommunityRegistry) Dir() string {
	return r.dir
}

// Backup writes a consistent copy of communities.db to path (VACUUM INTO,
// safe while the service runs). Holding r.mu keeps registry writes out.
func (r *CommunityRegistry) Backup(path string) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	_, err := r.db.Exec(`VACUUM INTO ?`, path)
	return err
}

func copyCommunity(c *model.Community) model.Community {
	cp := *c
	cp.Moderators = append([]model.CommunityModerator(nil), c.Moderators...)
	return cp
}

// Lookup returns the ID of the active community with the given name
// (case-insensitive).
func (r *CommunityRegistry) Lookup(name string) (int64, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	id, ok := r.byName[communityKey(name)]
	return id, ok
}

// ShardAssigner chooses the shard of a new community from its name and ID,
// given the number of communities (soft-deleted ones included) already on
// each shard.
type ShardAssigner func(name string, id int64, communities map[int]int) int

// SetShardAssigner sets how the shard of a new community is chosen. The
// shard is stored with the community and only changes when the community
// is moved, so renaming a community, or changing how new ones are
// assigned, does not move it. Without an assigner, communities are on
// shard 0.
func (r *CommunityRegistry) SetShardAssigner(assign ShardAssigner) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.assignShard = assign
}

// Shard returns the shard of the community id (including soft-deleted
// ones).
func (r *CommunityRegistry) Shard(id int64) (int, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	if c, ok := r.byID[id]; ok {
		return c.Shard, true
	}
	return 0, false
}

// SetShard records that the community id is now on shard; the caller moves
// its posts.
func (r *CommunityRegistry) SetShard(id int64, shard int) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	c, ok := r.byID[id]
	if !ok {
		return fmt.Errorf("%w: #%d", ErrCommunityNotFound, id)
	}
	now := time.Now().Unix()
	if _, err := r.db.Exec(`UPDATE community SET shard = ?, updated = ? WHERE id = ?`, shard, now, id); err != nil {
		return fmt.Errorf("set shard of community #%d failed: %w", id, err)
	}
	c.Shard = shard
	c.Updated = now
	return nil
}

// insertLocked registers a new community named name, on the shard that
// assignShard chooses; r.mu must be held.
func (r *CommunityRegistry) insertLocked(name string, now int64) (*model.Community, error) {
	tx, err := r.db.Begin()
	if err != nil {
		return nil, err
	}
	defer tx.Rollback()
	res, err := tx.Exec(`INSERT INTO community (name, published) VALUES (?, ?)`, name, now)
	if err != nil {
		return nil, fmt.Errorf("register community %s failed: %w", name, err)
	}
	id, err := res.LastInsertId()
	if err != nil {
		return nil, err
	}
	shard := 0
	if r.assignShard != nil {
		counts := make(map[int]int)
		for _, c := range r.byID {
			counts[c.Shard]++
		}
		shard = r.assignShard(name, id, counts)
	}
	if _, err := tx.Exec(`UPDATE community SET shard = ? WHERE id = ?`, shard, id); err != nil {
		return nil, err
	}
	if err := tx.Commit(); err != nil {
		return nil, err
	}
	c := &model.Community{ID: id, Name: name, Shard: shard, Published: now}
	r.byID[id] = c
	r.byName[communityKey(name)] = id
	return c, nil
}

// Ensure returns the ID of the active community with the given name,
// registering a new community if there is none.
func (r *CommunityRegistry) Ensure(name string) (int64, error) {
	name = strings.TrimSpace(name)
	if name == "" {
		return 0, fmt.Errorf("community name required")
	}
	if id, ok := r.Lookup(name); ok {
		return id, nil
	}

	r.mu.Lock()
	defer r.mu.Unlock()
	if id, ok := r.byName[communityKey(name)]; ok {
		return id, nil
	}
	c, err := r.insertLocked(name, time.Now().Unix())
	if err != nil {
		return 0, err
	}
	return c.ID, nil
}

// Upsert mirrors a board header: it sets the fields of the active
// community named in.Name (registering it if needed) from in, and replaces
// its moderators with in.Moderators, and returns its ID. The name keeps
// the case it is registered with (renaming is done by Rename), and the
// description is only changed if in.Description is not empty, as .BRD has
// none. It does not register a new community for a board (in.BID) that
// another active community mirrors.
func (r *CommunityRegistry) Upsert(in *model.Community) (int64, error) {
	name := strings.TrimSpace(in.Name)
	if name == "" {
		return 0, fmt.Errorf("community name required")
	}
	r.mu.Lock()
	defer r.mu.Unlock()

	now := time.Now().Unix()
	var c *model.Community
	if id, ok := r.byName[communityKey(name)]; ok {
		c = r.byID[id]
	} else {
		// A board renamed in .BRD but not in post.svc must not become a
		// second community: the board (bid) already has one.
		if in.BID != 0 {
			for _, other := range r.byID {
				if !other.Deleted && other.BID == in.BID {
					return 0, fmt.Errorf("%w: board #%d is community %s (#%d); rename or delete it first",
						ErrCommunityExists, in.BID, other.Name, other.ID)
				}
			}
		}
		var err error
		if c, err = r.insertLocked(name, now); err != nil {
			return 0, err
		}
	}

	updated := copyCommunity(c)
	updated.Title = in.Title
	if in.Description != "" {
		updated.Description = in.Description
	}
	updated.Category = in.Category
	updated.Flags = in.Flags
	updated.BID = in.BID
	updated.GroupBID = in.GroupBID
	updated.Group = in.Group
	updated.Level = in.Level
	updated.PostLimitLogins = in.PostLimitLogins
	updated.PostLimitBadpost = in.PostLimitBadpost
	updated.PetitionLimitLogins = in.PetitionLimitLogins
	updated.PetitionLimitBadpost = in.PetitionLimitBadpost
	updated.FastCommentPause = in.FastCommentPause
	updated.ModeratorNotes = in.ModeratorNotes
	updated.Updated = now
	updated.Derive()

	// Moderators keep when post.svc first saw them.
	since := make(map[model.PersonID]int64, len(c.Moderators))
	for _, m := range c.Moderators {
		since[m.PersonID] = m.Published
	}
	seen := make(map[model.PersonID]bool)
	updated.Moderators = nil
	for _, m := range in.Moderators {
		if m.PersonID == 0 || seen[m.PersonID] {
			continue
		}
		seen[m.PersonID] = true
		m.CommunityID = c.ID
		if p, ok := since[m.PersonID]; ok {
			m.Published = p
		} else {
			m.Published = now
		}
		updated.Moderators = append(updated.Moderators, m)
	}
	model.SortModerators(updated.Moderators)

	tx, err := r.db.Begin()
	if err != nil {
		return 0, err
	}
	defer tx.Rollback()
	if _, err := tx.Exec(`UPDATE community SET title = ?, description = ?, category = ?, flags = ?, bid = ?, group_bid = ?,
		group_name = ?, level = ?, post_limit_logins = ?, post_limit_badpost = ?, petition_limit_logins = ?,
		petition_limit_badpost = ?, fastcomment_pause = ?, moderator_notes = ?, updated = ? WHERE id = ?`,
		updated.Title, updated.Description, updated.Category, updated.Flags, updated.BID, updated.GroupBID,
		updated.Group, updated.Level, updated.PostLimitLogins, updated.PostLimitBadpost, updated.PetitionLimitLogins,
		updated.PetitionLimitBadpost, updated.FastCommentPause, updated.ModeratorNotes, updated.Updated, c.ID); err != nil {
		return 0, fmt.Errorf("update community %s failed: %w", name, err)
	}
	if _, err := tx.Exec(`DELETE FROM community_moderator WHERE community_id = ?`, c.ID); err != nil {
		return 0, err
	}
	for _, m := range updated.Moderators {
		if _, err := tx.Exec(`INSERT INTO community_moderator (community_id, person_id, person_name, published) VALUES (?, ?, ?, ?)`,
			c.ID, int64(m.PersonID), m.PersonName, m.Published); err != nil {
			return 0, fmt.Errorf("set moderators of %s failed: %w", name, err)
		}
	}
	if err := tx.Commit(); err != nil {
		return 0, err
	}
	*c = updated
	return c.ID, nil
}

// Name returns the name of the community with the given ID, including
// soft-deleted ones, or "" if the ID is unknown.
func (r *CommunityRegistry) Name(id int64) string {
	r.mu.RLock()
	defer r.mu.RUnlock()
	if c, ok := r.byID[id]; ok {
		return c.Name
	}
	return ""
}

// Get returns a copy of the community id (with its moderators).
func (r *CommunityRegistry) Get(id int64) (model.Community, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	if c, ok := r.byID[id]; ok {
		return copyCommunity(c), true
	}
	return model.Community{}, false
}

// GetByName returns a copy of the active community with the given name.
func (r *CommunityRegistry) GetByName(name string) (model.Community, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	if id, ok := r.byName[communityKey(name)]; ok {
		return copyCommunity(r.byID[id]), true
	}
	return model.Community{}, false
}

// Active reports whether id is a registered community that is not
// soft-deleted.
func (r *CommunityRegistry) Active(id int64) bool {
	r.mu.RLock()
	defer r.mu.RUnlock()
	c, ok := r.byID[id]
	return ok && !c.Deleted
}

// Rename changes the name of the active community oldName to newName and
// returns its ID. It fails if newName is used by another active community.
// Changing only the case of the name is allowed.
func (r *CommunityRegistry) Rename(oldName, newName string) (int64, error) {
	newName = strings.TrimSpace(newName)
	if newName == "" {
		return 0, fmt.Errorf("new community name required")
	}
	r.mu.Lock()
	defer r.mu.Unlock()

	oldKey, newKey := communityKey(oldName), communityKey(newName)
	id, ok := r.byName[oldKey]
	if !ok {
		return 0, fmt.Errorf("%w: %s", ErrCommunityNotFound, oldName)
	}
	if other, taken := r.byName[newKey]; taken && other != id {
		return 0, fmt.Errorf("%w: %s", ErrCommunityExists, newName)
	}
	now := time.Now().Unix()
	if _, err := r.db.Exec(`UPDATE community SET name = ?, updated = ? WHERE id = ?`, newName, now, id); err != nil {
		return 0, fmt.Errorf("rename community %s failed: %w", oldName, err)
	}
	delete(r.byName, oldKey)
	r.byName[newKey] = id
	r.byID[id].Name = newName
	r.byID[id].Updated = now
	return id, nil
}

// SoftDelete marks the active community name as deleted and returns its ID.
// Its posts keep the ID, and the name can be registered again by a new
// community (with a new ID).
func (r *CommunityRegistry) SoftDelete(name string) (int64, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	key := communityKey(name)
	id, ok := r.byName[key]
	if !ok {
		return 0, fmt.Errorf("%w: %s", ErrCommunityNotFound, name)
	}
	now := time.Now().Unix()
	if _, err := r.db.Exec(`UPDATE community SET deleted_at = ?, updated = ? WHERE id = ?`, now, now, id); err != nil {
		return 0, fmt.Errorf("delete community %s failed: %w", name, err)
	}
	delete(r.byName, key)
	r.byID[id].Deleted = true
	r.byID[id].DeletedAt = now
	r.byID[id].Updated = now
	return id, nil
}

// Remove permanently removes the community id (and its moderators) from
// the registry. The caller is responsible for removing its posts first.
func (r *CommunityRegistry) Remove(id int64) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	c, ok := r.byID[id]
	if !ok {
		return fmt.Errorf("%w: #%d", ErrCommunityNotFound, id)
	}
	tx, err := r.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if _, err := tx.Exec(`DELETE FROM community_moderator WHERE community_id = ?`, id); err != nil {
		return err
	}
	if _, err := tx.Exec(`DELETE FROM community WHERE id = ?`, id); err != nil {
		return fmt.Errorf("remove community #%d failed: %w", id, err)
	}
	if err := tx.Commit(); err != nil {
		return err
	}
	if !c.Deleted {
		delete(r.byName, communityKey(c.Name))
	}
	delete(r.byID, id)
	return nil
}

// List returns all registered communities (including soft-deleted ones)
// ordered by ID.
func (r *CommunityRegistry) List() []model.Community {
	r.mu.RLock()
	defer r.mu.RUnlock()
	list := make([]model.Community, 0, len(r.byID))
	for _, c := range r.byID {
		list = append(list, copyCommunity(c))
	}
	sort.Slice(list, func(i, j int) bool { return list[i].ID < list[j].ID })
	return list
}
