package main

import (
	"flag"
	"fmt"
	"os"
	"time"

	"pttbbs/nest/storage"
)

// nest.ctl extract-backup (was export-community): writes the posts of one
// community from a backup (e.g. a daily-backup copy on another host, mounted
// or rsynced back) into files, or lists its communities; read only and
// without nest.svc or meta.sqlite3.
func extractBackup(name string, args []string) int {
	fset := flag.NewFlagSet(name, flag.ContinueOnError)
	fset.Usage = func() {
		fmt.Fprintf(fset.Output(), "Usage: nest.ctl %s [flags] <community> <backup_dir>...\n", name)
		fmt.Fprintf(fset.Output(), "       nest.ctl %s -list <backup_dir>...\n", name)
		fmt.Fprintln(fset.Output(), "  backup_dir: an engine data dir (with posts.pebble) or a dir with shard-N subdirs;")
		fmt.Fprintln(fset.Output(), "  for daily-backup -dest copies, give every <dest>/shard-*/<name>.")
		fset.PrintDefaults()
	}
	out := fset.String("o", "", "output `dir` (default ./<community>)")
	id := fset.Int64("id", 0, "community ID, e.g. of a deleted board whose name was reused")
	deleted := fset.Bool("deleted", false, "also export deleted and removed posts, with their content")
	utf8 := fset.Bool("utf8", false, "write UTF-8 instead of Big5")
	legacy := fset.Bool("legacy-format", false, "render comments in the legacy push format")
	list := fset.Bool("list", false, "list the communities in the backup instead")
	if err := fset.Parse(args); err != nil {
		return 2
	}
	if *list {
		return listBackupCommunities(name, fset.Args())
	}
	if fset.NArg() < 2 {
		fset.Usage()
		return 2
	}
	community := fset.Arg(0)
	dirs, err := storage.ExpandVerifyDirs(fset.Args()[1:])
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
		return 2
	}
	communities := storage.FindCommunitiesDB(dirs)
	if communities == "" && *id == 0 {
		fmt.Fprintf(os.Stderr, "%s: no communities.db in the backup dirs; give -id\n", name)
		return 2
	}
	if *out == "" {
		*out = community
	}

	start := time.Now()
	res, err := storage.ExportCommunity(storage.ExportCommunityOptions{
		Dirs:          dirs,
		CommunitiesDB: communities,
		Community:     community,
		CommunityID:   *id,
		TargetDir:     *out,
		LegacyFormat:  *legacy,
		Big5:          !*utf8,
		Deleted:       *deleted,
		ProgressFn: func(dir string, scanned, exported int) {
			fmt.Fprintf(os.Stderr, "\r%s: %d posts scanned, %d exported", dir, scanned, exported)
		},
	})
	fmt.Fprintln(os.Stderr)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
		return 1
	}
	if res.Dir == "" {
		fmt.Printf("Community %s (#%d) has no posts in %d dir(s).\n", res.Name, res.CommunityID, len(dirs))
		return 0
	}
	fmt.Printf("Exported %d posts of %s (#%d) from %s into %s in %v",
		res.Exported, res.Name, res.CommunityID, res.Dir, *out, time.Since(start).Round(time.Second))
	if res.Skipped > 0 {
		fmt.Printf(" (%d deleted or removed left out; -deleted exports them)", res.Skipped)
	}
	fmt.Println(".")
	return 0
}

// listBackupCommunities prints the communities of the backup in dirs.
func listBackupCommunities(name string, args []string) int {
	if len(args) == 0 {
		fmt.Fprintf(os.Stderr, "Usage: nest.ctl %s -list <backup_dir>...\n", name)
		return 2
	}
	dirs, err := storage.ExpandVerifyDirs(args)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
		return 2
	}
	db := storage.FindCommunitiesDB(dirs)
	if db == "" {
		fmt.Fprintf(os.Stderr, "%s: no communities.db in the backup dirs\n", name)
		return 1
	}
	cs, err := storage.ListBackupCommunities(db)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: %v\n", name, err)
		return 1
	}
	fmt.Printf("%8s %5s  %s\n", "ID", "SHARD", "NAME")
	for _, c := range cs {
		gone := ""
		if c.TakenDown {
			gone = "  (taken down)"
		}
		fmt.Printf("%8d %5d  %s%s\n", c.ID, c.Shard, c.Name, gone)
	}
	return 0
}
