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

// CommunityRegistry keeps the communities (model.Community) and maps their
// names to IDs. It is shared by all shard engines and keeps a full copy of
// the table in memory.
//
// A soft-deleted community keeps its ID and name (so its posts can still
// be found and purged) but its name is free to be used by a new community.
type CommunityRegistry struct {
	db *sql.DB

	mu     sync.RWMutex
	byID   map[int64]*model.Community
	byName map[string]int64 // lower-cased name of active communities

	// assignShard chooses the shard of a new community; nil means 0.
	assignShard func(name string, id int64) int
}

func communityKey(name string) string {
	return strings.ToLower(strings.TrimSpace(name))
}

const communitySchema = `
	CREATE TABLE IF NOT EXISTS community (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		name TEXT NOT NULL,
		shard INTEGER NOT NULL DEFAULT 0,
		published INTEGER NOT NULL,
		updated INTEGER NOT NULL DEFAULT 0,
		deleted_at INTEGER NOT NULL DEFAULT 0
	);
	CREATE UNIQUE INDEX IF NOT EXISTS idx_community_active_name
		ON community(name COLLATE NOCASE) WHERE deleted_at = 0;
`

const communityColumns = `id, name, shard, published, updated, deleted_at`

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
		if err := rows.Scan(&c.ID, &c.Name, &c.Shard, &c.Published, &c.Updated, &c.DeletedAt); err != nil {
			return err
		}
		c.Deleted = c.DeletedAt != 0
		r.byID[c.ID] = c
		if !c.Deleted {
			r.byName[communityKey(c.Name)] = c.ID
		}
	}
	return rows.Err()
}

// Close closes the registry database.
func (r *CommunityRegistry) Close() error {
	return r.db.Close()
}

// Lookup returns the ID of the active community with the given name
// (case-insensitive).
func (r *CommunityRegistry) Lookup(name string) (int64, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	id, ok := r.byName[communityKey(name)]
	return id, ok
}

// SetShardAssigner sets how the shard of a new community is chosen (from
// its name and ID). The shard is stored with the community and never
// changes, so renaming a community, or changing how new ones are assigned,
// does not move it. Without an assigner, communities are on shard 0.
func (r *CommunityRegistry) SetShardAssigner(assign func(name string, id int64) int) {
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
		shard = r.assignShard(name, id)
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

// Get returns a copy of the community id.
func (r *CommunityRegistry) Get(id int64) (model.Community, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	if c, ok := r.byID[id]; ok {
		return *c, true
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

// Remove permanently removes the community id from the registry. The
// caller is responsible for removing its posts first.
func (r *CommunityRegistry) Remove(id int64) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	c, ok := r.byID[id]
	if !ok {
		return fmt.Errorf("%w: #%d", ErrCommunityNotFound, id)
	}
	if _, err := r.db.Exec(`DELETE FROM community WHERE id = ?`, id); err != nil {
		return fmt.Errorf("remove community #%d failed: %w", id, err)
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
		list = append(list, *c)
	}
	sort.Slice(list, func(i, j int) bool { return list[i].ID < list[j].ID })
	return list
}
