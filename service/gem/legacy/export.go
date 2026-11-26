package legacy

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"time"

	"golang.org/x/sys/unix"

	"pttbbs/gem/storage"
)

// ExportStats counts what an export wrote.
type ExportStats struct {
	Dirs, Files, Links, Indexes, Missing int
	Bytes                                int64
}

func (s ExportStats) String() string {
	return fmt.Sprintf("dirs=%d files=%d links=%d indexes=%d missing=%d bytes=%d", s.Dirs, s.Files, s.Links, s.Indexes, s.Missing, s.Bytes)
}

// ExportRoot writes the gem area root as files under dest (which must not
// exist, or be empty): a .DIR in each directory and one file per article,
// with their times, as man/ has them. Copies are written in full.
func ExportRoot(st *storage.Store, board, dest string) (ExportStats, error) {
	var stats ExportStats
	if des, err := os.ReadDir(dest); err == nil && len(des) > 0 {
		return stats, fmt.Errorf("%s is not empty", dest)
	}
	if !st.HasRoot(board) {
		return stats, &storage.Error{Code: storage.ErrNoRoot.Code, Detail: board}
	}
	err := exportDir(st, board, nil, dest, &stats)
	return stats, err
}

func setTime(p string, mtime int64) {
	if mtime > 0 {
		t := time.Unix(mtime, 0)
		os.Chtimes(p, t, t)
	}
}

func exportDir(st *storage.Store, board string, path []string, dest string, stats *ExportStats) error {
	l, err := st.List(board, path)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(dest, 0755); err != nil {
		return err
	}
	// An empty directory gets no .DIR, as mbbsd leaves it.
	if len(l.Entries) == 0 {
		return nil
	}
	f, err := os.Create(filepath.Join(dest, ".DIR"))
	if err != nil {
		return err
	}
	w := bufio.NewWriter(f)
	for i := range l.Entries {
		w.Write(l.Entries[i].Header[:])
	}
	if err := w.Flush(); err != nil {
		f.Close()
		return err
	}
	if err := f.Close(); err != nil {
		return err
	}
	for i := range l.Entries {
		e := &l.Entries[i]
		name := e.Name()
		if !storage.ValidName(name) {
			stats.Missing++
			continue
		}
		p := filepath.Join(dest, name)
		switch e.Kind {
		case storage.KindFile:
			data, err := st.ReadBlob(e.Blob)
			if err != nil {
				return fmt.Errorf("%s: %w", p, err)
			}
			if err := os.WriteFile(p, data, 0644); err != nil {
				return err
			}
			setTime(p, e.Mtime)
			stats.Files++
			stats.Bytes += int64(len(data))
		case storage.KindDir:
			if err := exportDir(st, board, append(slices.Clone(path), name), p, stats); err != nil {
				return err
			}
			setTime(p, e.Mtime)
			stats.Dirs++
		case storage.KindIndex:
			// The table of contents as it is now, a file (mbbsd without
			// gem.svc shows it as any other).
			data, err := Index(st, board)
			if err != nil {
				return err
			}
			if err := os.WriteFile(p, data, 0644); err != nil {
				return err
			}
			setTime(p, e.Mtime)
			stats.Indexes++
		case storage.KindLink:
			if err := os.Symlink(e.Target, p); err != nil {
				return err
			}
			if e.Mtime > 0 {
				tv := unix.NsecToTimeval(e.Mtime * 1e9)
				unix.Lutimes(p, []unix.Timeval{tv, tv})
			}
			stats.Links++
		default:
			stats.Missing++
		}
	}
	return nil
}
