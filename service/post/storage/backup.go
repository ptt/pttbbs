package storage

import (
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"regexp"

	"github.com/cockroachdb/pebble"
	"golang.org/x/sys/unix"
)

// Online backup of the post storage.
//
// Each Pebble DB is copied with pebble's Checkpoint, which hard-links the
// immutable SSTs when the destination is on the same filesystem and copies only
// the MANIFEST/WAL/OPTIONS, so it takes seconds and very little extra space. The
// SQLite metadata DB is copied with VACUUM INTO, which is safe while the service
// keeps running in WAL mode.
//
// The destination given to Backup is either:
//
//   - a backup name (e.g. "20261107-1500"): every engine writes its own copy to
//     <DataDir>/backup/<name>, on the same disk as the engine, so the copy is
//     made with hard links even when the engines live on different disks. Ship
//     these directories elsewhere (rsync, tar) afterwards.
//   - an absolute path: a single engine writes to that path, and the engines of
//     a ShardedEngine write to <path>/shard-<N>. Engines on another filesystem
//     than the path are copied in full.
//
// The Pebble DBs are checkpointed before SQLite on purpose: SQLite allocates the
// post IDs, so a SQLite copy that is slightly newer than Pebble only has a few
// extra metadata rows, while an older one could let a restored service reuse an
// ID that already exists in Pebble.
//
// To restore, put the backed-up meta.sqlite3 back together with the Pebble DBs
// and do NOT run `post.svc -rebuild`: SQLite holds a few things Pebble does not
// (e.g. titles changed by UpdatePostTitle, and the counters of deleted posts
// imported from .DIR), which a rebuild would lose. Rebuild only when
// meta.sqlite3 is missing or corrupt.

// BackupSubdir is the directory under each engine's DataDir that holds named
// backups.
const BackupSubdir = "backup"

var backupNameRE = regexp.MustCompile(`^[A-Za-z0-9_][A-Za-z0-9._-]*$`)

// IsValidBackupName reports whether name can be used as a named backup.
func IsValidBackupName(name string) bool {
	return backupNameRE.MatchString(name)
}

// Backup writes an online copy of the storage to dest (a backup name or an
// absolute path, see above) and returns the directories written. Destinations
// must not exist yet. Each copy is built in a temporary sibling directory and
// all of them are renamed into place only when every engine succeeded, so a
// failed backup leaves nothing behind.
func (e *Engine) Backup(dest string) ([]string, error) {
	return backupEngines([]*Engine{e}, dest, false)
}

// Backup writes every shard; see Engine.Backup.
func (s *ShardedEngine) Backup(dest string) ([]string, error) {
	return backupEngines(s.engines, dest, true)
}

func backupEngines(engines []*Engine, dest string, sharded bool) ([]string, error) {
	dirs := make([]string, len(engines))
	named := false
	switch {
	case IsValidBackupName(dest):
		named = true
		for i, e := range engines {
			dirs[i] = filepath.Join(e.cfg.DataDir, BackupSubdir, dest)
		}
	case filepath.IsAbs(dest):
		dest = filepath.Clean(dest)
		for i := range engines {
			if sharded {
				dirs[i] = filepath.Join(dest, fmt.Sprintf("shard-%d", i))
			} else {
				dirs[i] = dest
			}
		}
	default:
		return nil, fmt.Errorf("invalid backup destination %q: want a name ([A-Za-z0-9._-]) or an absolute path", dest)
	}

	for _, dir := range dirs {
		if _, err := os.Lstat(dir); err == nil {
			return nil, fmt.Errorf("backup destination %s already exists", dir)
		} else if !errors.Is(err, os.ErrNotExist) {
			return nil, err
		}
	}

	tmpDirs := make([]string, 0, len(engines))
	cleanup := func() {
		for _, tmp := range tmpDirs {
			os.RemoveAll(tmp)
		}
	}
	for i, e := range engines {
		if err := os.MkdirAll(filepath.Dir(dirs[i]), 0755); err != nil {
			cleanup()
			return nil, err
		}
		tmp := fmt.Sprintf("%s.tmp-%d", dirs[i], os.Getpid())
		if err := os.RemoveAll(tmp); err != nil {
			cleanup()
			return nil, err
		}
		tmpDirs = append(tmpDirs, tmp)
		if named && !sameFilesystem(e.cfg.DataDir, filepath.Dir(dirs[i])) {
			log.Printf("[post.svc] Warning: backup dir %s is not on the same filesystem as %s; files will be copied instead of hard-linked", dirs[i], e.cfg.DataDir)
		}
		if err := e.backupTo(tmp); err != nil {
			cleanup()
			if sharded {
				return nil, fmt.Errorf("shard %d (%s): %w", i, e.cfg.DataDir, err)
			}
			return nil, err
		}
	}
	for i, tmp := range tmpDirs {
		if err := os.Rename(tmp, dirs[i]); err != nil {
			// Undo the shards already renamed so the backup stays all-or-nothing.
			for _, done := range dirs[:i] {
				os.RemoveAll(done)
			}
			cleanup()
			return nil, err
		}
	}
	return dirs, nil
}

func sameFilesystem(a, b string) bool {
	var sa, sb unix.Stat_t
	if unix.Stat(a, &sa) != nil || unix.Stat(b, &sb) != nil {
		return true // Unknown; do not warn.
	}
	return sa.Dev == sb.Dev
}

// backupTo writes the engine's DBs into dir, which must not exist yet.
func (e *Engine) backupTo(dir string) error {
	if err := os.Mkdir(dir, 0755); err != nil {
		return err
	}

	// Persist the in-memory vote/comment counter deltas into SQLite first.
	e.FlushDirtyDeltas()

	for _, db := range []struct {
		name string
		db   *pebble.DB
	}{
		{"posts.pebble", e.postsDB},
		{"comments.pebble", e.commentsDB},
		{"votes.pebble", e.votesDB},
		{"history.pebble", e.historyDB},
	} {
		// Writes use pebble.NoSync, so the WAL must be flushed and synced first
		// or the checkpoint would miss the latest writes.
		if err := db.db.Checkpoint(filepath.Join(dir, db.name), pebble.WithFlushedWAL()); err != nil {
			return fmt.Errorf("checkpoint %s: %w", db.name, err)
		}
	}

	// Holding metaWriteMu keeps the counter flusher and other SQLite writers
	// out while the copy is made; readers are not blocked.
	e.metaWriteMu.Lock()
	defer e.metaWriteMu.Unlock()
	if _, err := e.metaDB.Exec(`VACUUM INTO ?`, filepath.Join(dir, "meta.sqlite3")); err != nil {
		return fmt.Errorf("backup meta.sqlite3: %w", err)
	}
	return nil
}
