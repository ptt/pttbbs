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
	"time"

	"pttbbs/gem/storage"
)

// gem.ctl daily-backup, for cron (as nest.ctl daily-backup): an online
// Pebble checkpoint via BACKUP <name>, an optional incremental copy to
// another disk (-dest; immutable tables are hard-linked to the previous
// copy, so `rsync -aH` keeps the links), optional -verify, then pruning of
// "daily-" backups only (hand-made ones are kept).
//
// To restore: stop gem.svc, replace <data dir>/gem.pebble and
// contents.pebble by the backup's index and contents, start gem.svc.

const dailyBackupPrefix = "daily-"

var tmpDirRE = regexp.MustCompile(`\.tmp-[0-9]+$`)

func dailyBackup(args []string) int {
	fset := flag.NewFlagSet("daily-backup", flag.ContinueOnError)
	fset.Usage = func() {
		fmt.Fprintln(fset.Output(), "Usage: gem.ctl daily-backup [flags] [name]")
		fmt.Fprintln(fset.Output(), "  name defaults to daily-YYYYMMDD-HHMM; only daily-* backups are pruned.")
		fset.PrintDefaults()
	}
	dest := fset.String("dest", "", "copy the backup to `dir`/<name> (e.g. a backup disk)")
	keepLocal := fset.Int("keep-local", 2, "hard-linked checkpoints to keep next to the data (they pin deleted tables on disk)")
	keep := fset.Int("keep", 14, "copies to keep under -dest")
	verify := fset.Bool("verify", false, "verify the copy (or the checkpoint without -dest) read only")
	if err := fset.Parse(args); err != nil {
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
	if strings.ContainsAny(name, "/\\ ") || name == "." || name == ".." {
		fmt.Fprintf(os.Stderr, "daily-backup: invalid name %q\n", name)
		return 2
	}

	start := time.Now()
	w, err := cl.Simple("BACKUP " + name)
	if err != nil || len(w) == 0 {
		fmt.Fprintf(os.Stderr, "daily-backup: %v\n", err)
		return 1
	}
	checkpoint := w[0]
	fmt.Printf("Checkpoint %s taken in %v.\n", checkpoint, time.Since(start).Round(time.Millisecond))

	result := checkpoint
	rc := 0
	if *dest != "" {
		t := time.Now()
		if err := os.MkdirAll(*dest, 0755); err != nil {
			fmt.Fprintf(os.Stderr, "daily-backup: %v\n", err)
			return 1
		}
		removeStaleTmpDirs(*dest)
		prev := latestBackupDir(*dest)
		dst := filepath.Join(*dest, name)
		linked, copied, err := copyIncremental(checkpoint, dst, prev)
		if err != nil {
			fmt.Fprintf(os.Stderr, "daily-backup: copy: %v\n", err)
			return 1
		}
		result = dst
		fmt.Printf("Copied to %s in %v (copied %s, hard-linked %s).\n", dst,
			time.Since(t).Round(time.Second), humanBytes(copied), humanBytes(linked))
	}
	if *verify {
		ok, err := verifyBackup(result)
		if err != nil || !ok {
			// Keep everything for inspection; do not prune good old backups.
			fmt.Fprintf(os.Stderr, "daily-backup: %s does not verify (%v): nothing pruned\n", result, err)
			return 1
		}
	}
	var errs []error
	if *dest != "" {
		n, err := pruneBackups(*dest, *keep)
		errs = append(errs, err)
		if n > 0 {
			fmt.Printf("Pruned %d old copies.\n", n)
		}
	}
	n, err := pruneBackups(filepath.Dir(checkpoint), *keepLocal)
	errs = append(errs, err)
	if n > 0 {
		fmt.Printf("Pruned %d old checkpoints.\n", n)
	}
	if err := errors.Join(errs...); err != nil {
		fmt.Fprintf(os.Stderr, "daily-backup: prune: %v\n", err)
		rc = 1
	}
	if *dest == "" {
		fmt.Println("Note: without -dest the backup is on the same disk as the data.")
	}
	return rc
}

// verifyBackup opens a backup read only and checks its reference counts and
// totals, printing what it found.
func verifyBackup(dir string) (bool, error) {
	st, err := storage.OpenBackup(dir)
	if err != nil {
		return false, err
	}
	defer st.Close()
	rep, err := st.Verify(false)
	if err != nil {
		return false, err
	}
	fmt.Printf("%s: listings=%d blobs=%d entries=%d stored_bytes=%d pending=%d\n",
		dir, rep.Listings, rep.Blobs, rep.Entries, rep.StoredBytes, rep.Pending)
	for _, p := range rep.Problems {
		fmt.Println("  " + p)
	}
	return rep.OK(), nil
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

// copyIncremental copies the backup dir src to a new dst, hard-linking
// tables that prev (may be "") has with the same size and time. Times are
// kept so the next run can link to the copies.
func copyIncremental(src, dst, prev string) (linked, copied int64, err error) {
	if _, err := os.Lstat(dst); err == nil {
		return 0, 0, fmt.Errorf("%s already exists", dst)
	}
	tmp := fmt.Sprintf("%s.tmp-%d", dst, os.Getpid())
	if err := os.RemoveAll(tmp); err != nil {
		return 0, 0, err
	}
	linkWarned := false
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
			// Whole seconds: NFS and other servers may keep mtimes coarser
			// than the local file system.
			if oi, err := os.Stat(old); err == nil && oi.Size() == info.Size() && oi.ModTime().Unix() == info.ModTime().Unix() {
				err := os.Link(old, target)
				if err == nil {
					linked += info.Size()
					return nil
				}
				if !linkWarned {
					linkWarned = true
					fmt.Fprintf(os.Stderr, "warning: cannot hard-link in %s (%v); copying in full instead\n", filepath.Dir(dst), err)
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

// sharing prints what the gem areas of the backup dir share (read only,
// without gem.svc).
func sharing(dir string) error {
	st, err := storage.OpenBackup(dir)
	if err != nil {
		return err
	}
	defer st.Close()
	r, err := st.Sharing()
	if err != nil {
		return err
	}
	pct := func(a, b uint64) float64 {
		if b == 0 {
			return 0
		}
		return 100 * float64(a) / float64(b)
	}
	fmt.Printf("areas:     %d\n", r.Areas)
	fmt.Printf("as seen:   %d dirs, %d files, %d bytes\n", r.Dirs, r.Files, r.Bytes)
	fmt.Printf("stored:    %d listings, %d contents, %d bytes (%.1f%% of what is seen)\n",
		r.Listings, r.Blobs, r.StoredBytes, pct(r.StoredBytes, r.Bytes))
	fmt.Printf("dirs:      %d listings shared by %d more places (copies)\n", r.SharedListings, r.SharedListingRefs)
	fmt.Printf("files:     %d contents shared by %d more files, saving %d bytes (%.1f%%)\n",
		r.SharedBlobs, r.SharedBlobRefs, r.SavedBytes, pct(r.SavedBytes, r.Bytes))
	fmt.Printf("same dirs: %d listings (%d entries in them) are copies of another kept apart; %d empty\n",
		r.SameDirs, r.SameDirEntries, r.EmptyDirs)
	return nil
}
