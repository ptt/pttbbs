package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"pttbbs/post/storage"
)

// post.ctl daily-backup (alias inc-backup): an incremental backup suitable for
// cron.
//
//  1. Ask post.svc for a named online backup (BACKUP <name>): every engine
//     writes a hard-linked checkpoint to <data dir>/backup/<name> on its own
//     disk, in seconds.
//  2. Optionally copy each checkpoint to another disk (-dest or -cross). The
//     copy is incremental: SST files are immutable, so an SST that the previous
//     copy already has (same name, size and mtime) is hard-linked to it instead
//     of copied. Every copy is a complete, directly restorable directory, but
//     only new SSTs plus the small MANIFEST/WAL files and meta.sqlite3 take
//     space and I/O. `rsync -aH` of the copy root to a remote host keeps the
//     hard links.
//  3. Optionally verify the result read-only (-verify).
//  4. Prune old backups: only names starting with "daily-" are pruned, so
//     backups made by hand under other names are kept.

const dailyBackupPrefix = "daily-"

var tmpDirRE = regexp.MustCompile(`\.tmp-[0-9]+$`)

type shardBackup struct {
	index      int
	checkpoint string // <data dir>/backup/<name>
	copyRoot   string // where the copies of this shard live, or ""
	copyDir    string
	linked     int64
	copied     int64
	pruned     int
	report     *storage.VerifyReport
	err        error
	elapsed    time.Duration
}

func dailyBackup(args []string) int {
	fset := flag.NewFlagSet("daily-backup", flag.ContinueOnError)
	fset.Usage = func() {
		fmt.Fprintln(fset.Output(), "Usage: post.ctl daily-backup [flags] [name]   (alias: inc-backup)")
		fmt.Fprintln(fset.Output(), "  name defaults to daily-YYYYMMDD-HHMM; only daily-* backups are pruned.")
		fset.PrintDefaults()
	}
	dest := fset.String("dest", "", "copy shard N to `dir`/shard-N/<name> (e.g. a dedicated backup disk)")
	cross := fset.Bool("cross", false, "copy shard N to the next shard's disk: <next data dir>/backup/shard-N/<name>")
	keepLocal := fset.Int("keep-local", 2, "hard-linked checkpoints to keep next to each engine (they pin deleted SSTs on disk)")
	keep := fset.Int("keep", 14, "copies to keep per shard under -dest/-cross")
	verify := fset.Bool("verify", false, "verify the copies (or the checkpoints without -dest/-cross) read-only")
	if err := fset.Parse(args); err != nil {
		return 2
	}
	if *dest != "" && *cross {
		fmt.Fprintln(os.Stderr, "daily-backup: -dest and -cross are mutually exclusive")
		return 2
	}
	if *keepLocal < 1 || *keep < 1 {
		fmt.Fprintln(os.Stderr, "daily-backup: -keep-local and -keep must be at least 1")
		return 2
	}
	if *dest != "" {
		abs, err := filepath.Abs(*dest)
		if err != nil {
			fmt.Fprintf(os.Stderr, "daily-backup: %v\n", err)
			return 2
		}
		*dest = abs
	}
	name := dailyBackupPrefix + time.Now().Format("20060102-1504")
	if fset.NArg() > 0 {
		name = fset.Arg(0)
	}
	if !storage.IsValidBackupName(name) {
		fmt.Fprintf(os.Stderr, "daily-backup: invalid name %q\n", name)
		return 2
	}

	start := time.Now()
	resp, payload, err := sendCmd("BACKUP "+name+"\n", nil)
	if err != nil {
		fmt.Fprintf(os.Stderr, "daily-backup: %v\n", err)
		return 1
	}
	if !strings.HasPrefix(resp, "OK") {
		fmt.Fprintf(os.Stderr, "daily-backup: %s\n", resp)
		return 1
	}
	var checkpoints []string
	for _, l := range strings.Split(strings.TrimRight(string(payload), "\n"), "\n") {
		if l != "" {
			checkpoints = append(checkpoints, l)
		}
	}
	if len(checkpoints) == 0 {
		fmt.Fprintln(os.Stderr, "daily-backup: post.svc reported no backup dirs")
		return 1
	}
	if *cross && len(checkpoints) < 2 {
		fmt.Fprintln(os.Stderr, "daily-backup: -cross needs at least 2 engines; use -dest")
		return 1
	}
	fmt.Printf("Checkpoints %s taken in %v.\n", name, time.Since(start).Round(time.Millisecond))

	// <data dir>/backup/<name> -> <data dir>
	dataDir := func(checkpoint string) string { return filepath.Dir(filepath.Dir(checkpoint)) }
	shards := make([]*shardBackup, len(checkpoints))
	for i, cp := range checkpoints {
		s := &shardBackup{index: i, checkpoint: cp}
		switch {
		case *dest != "":
			s.copyRoot = filepath.Join(*dest, fmt.Sprintf("shard-%d", i))
		case *cross:
			next := dataDir(checkpoints[(i+1)%len(checkpoints)])
			s.copyRoot = filepath.Join(next, storage.BackupSubdir, fmt.Sprintf("shard-%d", i))
		}
		shards[i] = s
	}

	// The shards live on different disks, so handle them in parallel.
	var wg sync.WaitGroup
	for _, s := range shards {
		wg.Add(1)
		go func(s *shardBackup) {
			defer wg.Done()
			t := time.Now()
			s.err = s.run(name, *keep, *keepLocal, *verify)
			s.elapsed = time.Since(t)
		}(s)
	}
	wg.Wait()

	rc := 0
	for _, s := range shards {
		fmt.Printf("shard-%d: %s", s.index, s.checkpoint)
		if s.copyDir != "" {
			fmt.Printf(" -> %s (copied %s, hard-linked %s)", s.copyDir, humanBytes(s.copied), humanBytes(s.linked))
		}
		fmt.Printf(" in %v", s.elapsed.Round(time.Second))
		if s.pruned > 0 {
			fmt.Printf(", pruned %d old", s.pruned)
		}
		fmt.Println()
		if s.report != nil {
			s.report.Print(os.Stdout)
			if !s.report.OK() {
				rc = 1
			}
		}
		if s.err != nil {
			fmt.Printf("  ERROR: %v\n", s.err)
			rc = 1
		}
	}
	if *dest == "" && !*cross {
		fmt.Println("Note: without -dest or -cross the backups are on the same disks as the data.")
	}
	return rc
}

func (s *shardBackup) run(name string, keep, keepLocal int, verify bool) error {
	verifyDir := s.checkpoint
	if s.copyRoot != "" {
		if err := os.MkdirAll(s.copyRoot, 0755); err != nil {
			return err
		}
		removeStaleTmpDirs(s.copyRoot)
		prev := latestBackupDir(s.copyRoot)
		s.copyDir = filepath.Join(s.copyRoot, name)
		var err error
		if s.linked, s.copied, err = copyIncremental(s.checkpoint, s.copyDir, prev); err != nil {
			return fmt.Errorf("copy: %w", err)
		}
		verifyDir = s.copyDir
	}
	if verify {
		s.report = storage.VerifyDataDir(verifyDir)
		if !s.report.OK() {
			// Keep everything for inspection; do not prune good old backups.
			return nil
		}
	}
	var errs []error
	if s.copyRoot != "" {
		n, err := pruneBackups(s.copyRoot, keep)
		s.pruned += n
		errs = append(errs, err)
	}
	n, err := pruneBackups(filepath.Dir(s.checkpoint), keepLocal)
	s.pruned += n
	errs = append(errs, err)
	return errors.Join(errs...)
}

// backupDirs lists the completed daily-* backups in root, oldest first.
func backupDirs(root string) []os.DirEntry {
	entries, _ := os.ReadDir(root)
	var out []os.DirEntry
	for _, e := range entries {
		if e.IsDir() && strings.HasPrefix(e.Name(), dailyBackupPrefix) && !tmpDirRE.MatchString(e.Name()) {
			out = append(out, e)
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name() < out[j].Name() })
	return out
}

// latestBackupDir returns the newest completed backup in root (any name), or "".
func latestBackupDir(root string) string {
	entries, _ := os.ReadDir(root)
	var best string
	var bestTime time.Time
	for _, e := range entries {
		if !e.IsDir() || tmpDirRE.MatchString(e.Name()) {
			continue
		}
		if info, err := e.Info(); err == nil && info.ModTime().After(bestTime) {
			best, bestTime = filepath.Join(root, e.Name()), info.ModTime()
		}
	}
	return best
}

func pruneBackups(root string, keep int) (int, error) {
	dirs := backupDirs(root)
	pruned := 0
	var errs []error
	for len(dirs)-pruned > keep {
		if err := os.RemoveAll(filepath.Join(root, dirs[pruned].Name())); err != nil {
			errs = append(errs, err)
		}
		pruned++
	}
	return pruned, errors.Join(errs...)
}

// removeStaleTmpDirs removes half-written copies left by interrupted runs.
func removeStaleTmpDirs(root string) {
	entries, _ := os.ReadDir(root)
	for _, e := range entries {
		if e.IsDir() && tmpDirRE.MatchString(e.Name()) && !strings.HasSuffix(e.Name(), ".tmp-"+strconv.Itoa(os.Getpid())) {
			os.RemoveAll(filepath.Join(root, e.Name()))
		}
	}
}

// copyIncremental copies the backup dir src to dst (which must not exist).
// SST files that prev (the previous copy, may be "") already has with the
// same size and mtime are hard-linked instead of copied; everything else is
// copied with its mtime preserved, so tomorrow's run can link to it.
func copyIncremental(src, dst, prev string) (linked, copied int64, err error) {
	if _, err := os.Lstat(dst); err == nil {
		return 0, 0, fmt.Errorf("%s already exists", dst)
	}
	tmp := fmt.Sprintf("%s.tmp-%d", dst, os.Getpid())
	if err := os.RemoveAll(tmp); err != nil {
		return 0, 0, err
	}
	err = filepath.WalkDir(src, func(path string, d fs.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		rel, err := filepath.Rel(src, path)
		if err != nil {
			return err
		}
		target := filepath.Join(tmp, rel)
		info, err := d.Info()
		if err != nil {
			return err
		}
		if d.IsDir() {
			return os.MkdirAll(target, 0755)
		}
		if !info.Mode().IsRegular() {
			return nil
		}
		if prev != "" && strings.HasSuffix(d.Name(), ".sst") {
			old := filepath.Join(prev, rel)
			if oi, err := os.Stat(old); err == nil && oi.Size() == info.Size() && oi.ModTime().Equal(info.ModTime()) {
				if err := os.Link(old, target); err == nil {
					linked += info.Size()
					return nil
				}
			}
		}
		if err := copyFile(path, target, info); err != nil {
			return err
		}
		copied += info.Size()
		return nil
	})
	if err == nil {
		err = os.Rename(tmp, dst)
	}
	if err != nil {
		os.RemoveAll(tmp)
		return 0, 0, err
	}
	return linked, copied, nil
}

func copyFile(src, dst string, info os.FileInfo) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.OpenFile(dst, os.O_WRONLY|os.O_CREATE|os.O_EXCL, info.Mode().Perm())
	if err != nil {
		return err
	}
	if _, err := io.Copy(out, in); err != nil {
		out.Close()
		return err
	}
	if err := out.Sync(); err != nil {
		out.Close()
		return err
	}
	if err := out.Close(); err != nil {
		return err
	}
	return os.Chtimes(dst, info.ModTime(), info.ModTime())
}

func humanBytes(n int64) string {
	const unit = 1024
	if n < unit {
		return fmt.Sprintf("%d B", n)
	}
	div, exp := int64(unit), 0
	for m := n / unit; m >= unit; m /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %ciB", float64(n)/float64(div), "KMGTPE"[exp])
}
