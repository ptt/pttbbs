package rotate

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"pttbbs/rotatelog/archive"
)

const dateLayout = "2006-01-02"

// EntryState is what the state remembers of an entry.
type EntryState struct {
	// Last is the day (YYYY-MM-DD) it was last rotated.
	Last string `json:"last"`
}

// State is kept in $BBSHOME/db/rotatelog.db/state.json.
type State map[string]*EntryState

// StateDir is where the state and the lock are, relative to BBSHOME.
const StateDir = "db/rotatelog.db"

// Rotator rotates the entries of a configuration.
type Rotator struct {
	Home   string // BBSHOME
	Config *Config
	State  State
	Now    time.Time
	DryRun bool
	Out    io.Writer // what it does, a line each
}

func (r *Rotator) path(rel string) string { return filepath.Join(r.Home, rel) }

func (r *Rotator) printf(format string, args ...any) {
	if r.Out != nil {
		fmt.Fprintf(r.Out, format+"\n", args...)
	}
}

func day(t time.Time) time.Time {
	y, m, d := t.Date()
	return time.Date(y, m, d, 0, 0, 0, 0, t.Location())
}

// Occurrence is the last day on or before now when e is to be rotated.
func Occurrence(e *Entry, now time.Time) time.Time {
	today := day(now)
	switch e.Period {
	case Weekly:
		back := (int(today.Weekday()) - int(e.Weekday) + 7) % 7
		return today.AddDate(0, 0, -back)
	case Monthly:
		return today.AddDate(0, 0, 1-today.Day())
	}
	return today
}

// Next is the next day after now when e is to be rotated.
func Next(e *Entry, now time.Time) time.Time {
	occ := Occurrence(e, now)
	switch e.Period {
	case Weekly:
		return occ.AddDate(0, 0, 7)
	case Monthly:
		return occ.AddDate(0, 1, 0)
	}
	return occ.AddDate(0, 0, 1)
}

// Due tells whether e is to be rotated now: not done since its last
// occurrence. An entry never done is due only on the day it is to be
// rotated, so that a new weekly or monthly entry waits for its day.
func (r *Rotator) Due(e *Entry) bool {
	occ := Occurrence(e, r.Now).Format(dateLayout)
	st := r.State[e.Key()]
	if st == nil || st.Last == "" {
		return occ == day(r.Now).Format(dateLayout)
	}
	return st.Last < occ
}

// Run rotates the entries that are due, or all those in force (by path or
// name) whether due or not. It returns the errors met; an entry that fails
// is not marked done, except as noted in EntryState.
func (r *Rotator) Run(force map[string]bool) []error {
	var errs []error
	for _, e := range r.Config.Entries {
		if force != nil {
			if !force[e.Path] && !(e.Name != "" && force[e.Name]) {
				continue
			}
		} else if !r.Due(e) {
			continue
		}
		var err error
		switch e.Kind {
		case Archive:
			err = r.archive(e)
		case Snapshot:
			err = r.snapshot(e)
		}
		if err != nil {
			errs = append(errs, fmt.Errorf("%s: %w", e.Path, err))
			continue
		}
		if !r.DryRun {
			st := r.state(e)
			st.Last = day(r.Now).Format(dateLayout)
		}
	}
	return errs
}

func (r *Rotator) state(e *Entry) *EntryState {
	st := r.State[e.Key()]
	if st == nil {
		st = &EntryState{}
		r.State[e.Key()] = st
	}
	return st
}

// archivePath picks adm/YYYY/<name>/<name>-YYYYMMDD<suffix>, or -2, -3...
// if taken (see package archive, also used by the programs reading them).
func (r *Rotator) archivePath(name string, d time.Time, suffix string) string {
	return archive.NewPath(r.Home, name, d, suffix)
}

func exists(path string) bool {
	_, err := os.Lstat(path)
	return err == nil
}

func size(path string) (int64, bool) {
	st, err := os.Stat(path)
	if err != nil {
		return 0, false
	}
	return st.Size(), true
}

func human(n int64) string {
	switch {
	case n >= 10<<20:
		return fmt.Sprintf("%d MB", n>>20)
	case n >= 10<<10:
		return fmt.Sprintf("%d KB", n>>10)
	}
	return fmt.Sprintf("%d bytes", n)
}

// moveOut moves (or copies and empties, with copytruncate) src to dst.
func moveOut(e *Entry, src, dst string) error {
	if e.CopyTruncate {
		if err := copyFile(src, dst); err != nil {
			return err
		}
		return os.Truncate(src, 0)
	}
	err := os.Rename(src, dst)
	var le *os.LinkError
	if errors.As(err, &le) && errors.Is(le.Err, syscall.EXDEV) {
		if err = copyFile(src, dst); err == nil {
			err = os.Remove(src)
		}
	}
	return err
}

// compressInto compresses src into the archive dst (relative), logging it.
func (r *Rotator) compressInto(e *Entry, src, dst string) error {
	if r.DryRun {
		r.printf("%s: would compress %s into %s (%s)", e.Path, rel(r, src), dst,
			e.Compression)
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(r.path(dst)), 0755); err != nil {
		return err
	}
	start := time.Now()
	if err := compressFile(src, r.path(dst), e.Compression); err != nil {
		return err
	}
	in, _ := size(src)
	out, _ := size(r.path(dst))
	r.printf("%s: %s -> %s (%s -> %s, %.1fs)", e.Path, rel(r, src), dst,
		human(in), human(out), time.Since(start).Seconds())
	return nil
}

func rel(r *Rotator, p string) string {
	if s, err := filepath.Rel(r.Home, p); err == nil {
		return s
	}
	return p
}

// coveredDay is the last day a log covers, by the time of its last line:
// lines written on the day of the rotation (it runs after midnight) are the
// tail of the day before.
func coveredDay(mtime, now time.Time) time.Time {
	today := day(now)
	if d := day(mtime); d.Before(today) {
		return d
	}
	return today.AddDate(0, 0, -1)
}

// archive: <path> compressed into adm/YYYY/<name>/<name>-YYYYMMDD and removed. It
// is first moved to <path>.<YYYYMMDD>.rotating, where it stays if it
// cannot be compressed, to be tried again by the next run.
func (r *Rotator) archive(e *Entry) error {
	src := r.path(e.Path)
	left, _ := filepath.Glob(src + ".*.rotating")
	for _, p := range left {
		ymd := strings.TrimSuffix(strings.TrimPrefix(p, src+"."), ".rotating")
		d, err := time.ParseInLocation("20060102", ymd, r.Now.Location())
		if err != nil {
			continue
		}
		if err := r.compressInto(e, p, r.archivePath(e.Name, d,
			e.Compression.Ext())); err != nil {
			return fmt.Errorf("%s is still not archived: %w", rel(r, p), err)
		}
		if !r.DryRun {
			os.Remove(p)
		}
	}

	fi, err := os.Stat(src)
	if err != nil || fi.Size() == 0 {
		r.printf("%s: missing or empty, nothing to archive", e.Path)
		return nil
	}
	covered := coveredDay(fi.ModTime(), r.Now)
	dst := r.archivePath(e.Name, covered, e.Compression.Ext())
	if r.DryRun {
		return r.compressInto(e, src, dst)
	}
	rotating := fmt.Sprintf("%s.%s.rotating", src, covered.Format("20060102"))
	if err := moveOut(e, src, rotating); err != nil {
		return err
	}
	if err := r.compressInto(e, rotating, dst); err != nil {
		return fmt.Errorf("left in %s, tried again next time: %w",
			rel(r, rotating), err)
	}
	return os.Remove(rotating)
}

// snapshot: <dir> packed into adm/YYYY/<name>/<name>-YYYYMMDD.tar.<ext>, then
// emptied.
func (r *Rotator) snapshot(e *Entry) error {
	src := r.path(e.Path)
	st, err := os.Stat(src)
	if os.IsNotExist(err) {
		r.printf("%s: missing, nothing to pack", e.Path)
		return nil
	} else if err != nil {
		return err
	}
	if !st.IsDir() {
		return fmt.Errorf("not a directory")
	}
	ents, err := os.ReadDir(src)
	if err != nil {
		return err
	}
	if len(ents) == 0 {
		r.printf("%s: empty, nothing to pack", e.Path)
		return nil
	}
	dst := r.archivePath(e.Name, day(r.Now), ".tar"+e.Compression.Ext())
	if r.DryRun {
		r.printf("%s: would pack %d entries into %s and empty it", e.Path,
			len(ents), dst)
		return nil
	}
	moving := src + ".rotating"
	if exists(moving) {
		return fmt.Errorf("%s is there (left by a run that failed): pack or "+
			"remove it first", rel(r, moving))
	}
	if err := os.Rename(src, moving); err != nil {
		return err
	}
	if err := os.Mkdir(src, st.Mode().Perm()); err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(r.path(dst)), 0755); err != nil {
		return err
	}
	start := time.Now()
	if err := tarDir(moving, e.Name, r.path(dst), e.Compression); err != nil {
		return fmt.Errorf("left in %s: %w", rel(r, moving), err)
	}
	out, _ := size(r.path(dst))
	r.printf("%s: packed into %s (%s, %.1fs)", e.Path, dst, human(out),
		time.Since(start).Seconds())
	return os.RemoveAll(moving)
}

// LoadState reads the state; none is an empty one.
func LoadState(home string) (State, error) {
	b, err := os.ReadFile(filepath.Join(home, StateDir, "state.json"))
	if os.IsNotExist(err) {
		return State{}, nil
	} else if err != nil {
		return nil, err
	}
	s := State{}
	if err := json.Unmarshal(b, &s); err != nil {
		return nil, fmt.Errorf("%s/state.json: %w", StateDir, err)
	}
	return s, nil
}

// SaveState writes the state (through a temporary file).
func SaveState(home string, s State) error {
	dir := filepath.Join(home, StateDir)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}
	b, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}
	tmp := filepath.Join(dir, "state.json.tmp")
	if err := os.WriteFile(tmp, append(b, '\n'), 0644); err != nil {
		return err
	}
	return os.Rename(tmp, filepath.Join(dir, "state.json"))
}

// Lock keeps another rotatelog from running at the same time; close the
// file to release it.
func Lock(home string) (*os.File, error) {
	dir := filepath.Join(home, StateDir)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return nil, err
	}
	f, err := os.OpenFile(filepath.Join(dir, "lock"), os.O_RDWR|os.O_CREATE, 0644)
	if err != nil {
		return nil, err
	}
	if err := syscall.Flock(int(f.Fd()), syscall.LOCK_EX|syscall.LOCK_NB); err != nil {
		f.Close()
		return nil, fmt.Errorf("another rotatelog is running")
	}
	return f, nil
}
