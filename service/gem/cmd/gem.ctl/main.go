// gem.ctl talks to gem.svc: imports and exports gem areas, lists and
// changes them, checks the database.
package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"pttbbs/bbs"
	"pttbbs/big5uao"
	"pttbbs/gem/client"
	"pttbbs/gem/config"
	"pttbbs/gem/protocol"
	"pttbbs/gem/storage"
	"pttbbs/svcctl"
)

const usage = `Usage: gem.ctl [-socket path] <command> [args...]

A <root> is a gem area: board:<name> (or just the name of the board),
user:<id> (the mail folder of a person) or site (the area of the site).

Commands:
  ping
  roots                              Gem areas in gem.svc
  stat <root>                        Totals of a gem area
  quota <root>                       Quotas of a gem area (0: none)
  ls <root> [path]                   List a directory ("." or empty: the top)
  cat <root> <path/name>             Print a file (the bytes kept, not converted)
  index <root>                       Table of contents (what mandex wrote to .index)

Titles (ls, mkdir) and the index are converted from and to UTF-8 when the
locale ($LC_ALL, $LC_CTYPE, $LANG) or -encoding says UTF-8; gem.svc keeps
the bytes of the BBS (Big5).
  import [-replace] [-dir D] <root>...
                                     Import from its files (man/boards/<c>/<board>,
                                     home/<c>/<id>/man, man/)
  import-all [-j N] [-replace] [-users]
                                     Import the site, every board under man/boards
                                     and, with -users, every mail folder; then
                                     resolve-links (-no-resolve: not)
  resolve-links [-old-home H,...]    Replace the symbolic links of all areas by what
                                     they point to (shared, copy on write); absolute
                                     targets under BBSHOME or an old BBSHOME
                                     (default /home/bbs) are followed
  export <root> <dir>                Write a gem area as files (new or empty dir)
  mkdir <root> <path> <title>        Add a directory
  cp [-name N] <root> <path/name> <to_root> <to_path>
                                     Copy (a directory is shared: copy on write)
  mv <root> <path/name> <to_root> <to_path>
  rm <root> <path/name>              Remove for good (a directory with all below it)
  delete [-by ID] <root> <path/name> Move to the trash of the area (as mbbsd deletes)
  trash <root>                       List the trash: name, kind, deleted, by, from
  restore <root> <name>              Move back from the trash where it was
  purge [-days N] | purge <root> <name>
                                     Remove for good from the trash what it has had
                                     for more than N days (default 30), or one entry
  expire [-days N] <root> <path>     Remove for good the files of a directory older
                                     than N days (default 5), e.g. Note SONGO
  rename-root <from> <to>
  delete-root <root>
  verify [-fix]                      Check reference counts and totals
  pending                            Listings waiting to be freed
  metrics                            Pebble's metrics (levels, compactions, caches)
  backup [name|/abs/path]            Pebble checkpoint (default under db/gem.db/backup)
  daily-backup [-dest DIR] [-keep N] [-keep-local N] [-verify] [name]
                                     Backup for cron: checkpoint, incremental copy to
                                     -dest, verify, prune old daily-* backups
  verify-backup <dir>                Verify a backup read only
  sharing <dir>                      What a backup shares: listings and contents kept
                                     once for several places, and directories alike
  ` + svcctl.Usage + `
`

var cl *client.Client

func die(err error) {
	fmt.Fprintf(os.Stderr, "gem.ctl: %v\n", err)
	os.Exit(1)
}

func ping(socket string) error {
	return (&client.Client{Socket: socket, Timeout: 5 * time.Second}).Ping()
}

// splitEntry splits "a/b/name" into ([a b], name).
func splitEntry(s string) ([]string, string, error) {
	p, err := storage.SplitPath(s)
	if err != nil {
		return nil, "", err
	}
	if len(p) == 0 {
		return nil, "", fmt.Errorf("%q names no entry", s)
	}
	return p[:len(p)-1], p[len(p)-1], nil
}

func mustPath(s string) []string {
	p, err := storage.SplitPath(s)
	if err != nil {
		die(err)
	}
	return p
}

// termUTF8 tells whether the terminal takes UTF-8: -encoding, else the
// locale ($LC_ALL, then $LC_CTYPE, then $LANG). gem.svc keeps the bytes of
// the BBS (Big5), shown as they are on other terminals.
var termUTF8 bool

func localeIsUTF8() bool {
	for _, v := range []string{"LC_ALL", "LC_CTYPE", "LANG"} {
		if l := os.Getenv(v); l != "" {
			l = strings.ToLower(l)
			return strings.Contains(l, "utf-8") || strings.Contains(l, "utf8")
		}
	}
	return false
}

// toTerm converts text of the BBS (Big5) for the terminal.
func toTerm(b []byte) string {
	if termUTF8 {
		return big5uao.Decode(b)
	}
	return string(b)
}

// fromTerm converts what was typed into text of the BBS.
func fromTerm(s string) []byte {
	if termUTF8 {
		return big5uao.Encode(s)
	}
	return []byte(s)
}

func title(h *storage.Header) string { return toTerm(h.Title()) }

func main() {
	bbsHome := os.Getenv("BBSHOME")
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}
	socket := flag.String("socket", config.Socket(bbsHome), "UNIX socket of gem.svc")
	encoding := flag.String("encoding", "auto", "Terminal encoding for titles and the index: auto (from the locale), utf-8 or big5 (as kept)")
	flag.Usage = func() { fmt.Fprint(os.Stderr, usage) }
	flag.Parse()
	args := flag.Args()
	if len(args) == 0 {
		flag.Usage()
		os.Exit(2)
	}
	switch strings.ToLower(*encoding) {
	case "auto", "":
		termUTF8 = localeIsUTF8()
	case "utf-8", "utf8":
		termUTF8 = true
	case "big5", "raw":
		termUTF8 = false
	default:
		die(fmt.Errorf("unknown -encoding %q", *encoding))
	}
	cl = &client.Client{Socket: *socket}
	cmd, args := args[0], args[1:]
	need := func(n int) {
		if len(args) < n {
			flag.Usage()
			os.Exit(2)
		}
	}

	switch cmd {
	case "restart":
		os.Exit(svcctl.Main(svcctl.Options{Name: "gem.svc", Socket: *socket, Ping: ping}, args))

	case "ping":
		if err := cl.Ping(); err != nil {
			die(err)
		}
		fmt.Println("PONG")

	case "roots":
		r, err := cl.Do("ROOTS")
		if err != nil {
			die(err)
		}
		lines, err := r.Lines(int(r.Int(0)))
		r.Close()
		if err != nil {
			die(err)
		}
		sort.Strings(lines)
		for _, l := range lines {
			fmt.Println(l)
		}

	case "stat":
		need(1)
		w, err := cl.Simple("STAT " + args[0])
		if err != nil {
			die(err)
		}
		fmt.Printf("files=%s dirs=%s links=%s missing=%s bytes=%s depth=%s nodes=%s\n", w[0], w[1], w[2], w[3], w[4], w[5], w[6])

	case "quota":
		need(1)
		w, err := cl.Simple("QUOTA " + args[0])
		if err != nil {
			die(err)
		}
		fmt.Printf("nodes=%s dirs=%s bytes=%s width=%s depth=%s copy=%s\n", w[0], w[1], w[2], w[3], w[4], w[5])

	case "ls":
		need(1)
		var path []string
		if len(args) > 1 {
			path = mustPath(args[1])
		}
		l, err := cl.List(args[0], path, 0, -1)
		if err != nil {
			die(err)
		}
		for i, r := range l.Records {
			size := fmt.Sprint(r.Bytes)
			if r.Kind == storage.KindDir {
				size = fmt.Sprintf("%d/%d", r.Nodes, r.Bytes)
			}
			flags := ""
			if m := r.Header.Filemode(); m&storage.FileHide != 0 {
				flags += "H"
			} else if m&storage.FileBM != 0 {
				flags += "B"
			}
			fmt.Printf("%5d %-7s %-20s %-2s %12s %-12s %s\n", i, r.Kind, r.Header.Name(), flags, size, r.Header.Owner(), title(&r.Header))
		}
		t := l.Totals
		fmt.Printf("total: %d entries; below: files=%d dirs=%d links=%d missing=%d bytes=%d depth=%d\n",
			l.Count, t.Files, t.Dirs, t.Links, t.Missing, t.Bytes, t.Depth)

	case "cat":
		need(2)
		path, name, err := splitEntry(args[1])
		if err != nil {
			die(err)
		}
		data, err := cl.Get(args[0], path, -1, name)
		if err != nil {
			die(err)
		}
		os.Stdout.Write(data)

	case "import":
		fs := flag.NewFlagSet("import", flag.ExitOnError)
		replace := fs.Bool("replace", false, "Replace an area already imported")
		dir := fs.String("dir", "", "Import from this directory (one board)")
		fs.Parse(args)
		if fs.NArg() == 0 || (*dir != "" && fs.NArg() != 1) {
			flag.Usage()
			os.Exit(2)
		}
		failed := false
		for _, b := range fs.Args() {
			line := "IMPORT " + b
			if *replace {
				line += " replace"
			}
			if *dir != "" {
				abs, err := filepath.Abs(*dir)
				if err != nil {
					die(err)
				}
				line += " " + abs
			}
			w, err := cl.Simple(line)
			if err != nil {
				fmt.Fprintf(os.Stderr, "%s: %v\n", b, err)
				failed = true
				continue
			}
			fmt.Printf("%s: %s (%sms)\n", b, strings.Join(w[1:], " "), w[0])
		}
		if failed {
			os.Exit(1)
		}

	case "import-all":
		fs := flag.NewFlagSet("import-all", flag.ExitOnError)
		jobs := fs.Int("j", 4, "Areas imported at the same time")
		replace := fs.Bool("replace", false, "Replace areas already imported (else they are skipped)")
		users := fs.Bool("users", false, "Also the mail folders of people (home/<c>/<id>/man)")
		noResolve := fs.Bool("no-resolve", false, "Leave the symbolic links (see resolve-links)")
		fs.Parse(args)
		code := importAll(bbsHome, *jobs, *replace, *users)
		if code == 0 && !*noResolve {
			code = resolveLinks(nil)
		}
		os.Exit(code)

	case "resolve-links":
		fs := flag.NewFlagSet("resolve-links", flag.ExitOnError)
		olds := fs.String("old-home", "", "Where BBSHOME was when links were made, comma separated (default /home/bbs)")
		fs.Parse(args)
		var homes []string
		if *olds != "" {
			homes = strings.Split(*olds, ",")
		}
		os.Exit(resolveLinks(homes))

	case "export":
		need(2)
		abs, err := filepath.Abs(args[1])
		if err != nil {
			die(err)
		}
		w, err := cl.Simple("EXPORT " + args[0] + " " + abs)
		if err != nil {
			die(err)
		}
		fmt.Println(strings.Join(w, " "))

	case "mkdir":
		need(3)
		var h storage.Header
		h.SetTitle(fromTerm(args[2]))
		h.SetOwner("SYSOP")
		rec, at, err := cl.Add("dir", args[0], mustPath(args[1]), -1, h, nil)
		if err != nil {
			die(err)
		}
		fmt.Printf("%d %s\n", at, rec.Header.Name())

	case "cp", "mv":
		fs := flag.NewFlagSet(cmd, flag.ExitOnError)
		newName := fs.String("name", "", "Name of the copy (default: the same)")
		fs.Parse(args)
		a := fs.Args()
		if len(a) < 4 {
			flag.Usage()
			os.Exit(2)
		}
		path, name, err := splitEntry(a[1])
		if err != nil {
			die(err)
		}
		src := storage.Ref{Root: a[0], Path: path, Index: -1, Name: name}
		var hp *storage.Header
		if *newName != "" {
			rec, _, _, err := cl.Entry(a[0], path, -1, name)
			if err != nil {
				die(err)
			}
			h := rec.Header
			h.SetName(*newName)
			hp = &h
		}
		verb := "COPY"
		if cmd == "mv" {
			verb = "MOVE"
		}
		rec, at, err := cl.CopyOrMove(verb, src, a[2], mustPath(a[3]), -1, hp)
		if err != nil {
			die(err)
		}
		fmt.Printf("%d %s\n", at, rec.Header.Name())

	case "rm":
		need(2)
		path, name, err := splitEntry(args[1])
		if err != nil {
			die(err)
		}
		if _, err := cl.Simple(fmt.Sprintf("REMOVE %s %s -1 %s", args[0], protocol.Path(path), name)); err != nil {
			die(err)
		}

	case "delete":
		fs := flag.NewFlagSet("delete", flag.ExitOnError)
		by := fs.String("by", "SYSOP", "Who deletes it")
		fs.Parse(args)
		if fs.NArg() != 2 {
			flag.Usage()
			os.Exit(2)
		}
		path, name, err := splitEntry(fs.Arg(1))
		if err != nil {
			die(err)
		}
		w, err := cl.Simple(fmt.Sprintf("DELETE %s %s -1 %s %s", fs.Arg(0), protocol.Path(path), name, *by))
		if err != nil {
			die(err)
		}
		fmt.Printf("in the trash as %s\n", w[0])

	case "trash":
		need(1)
		r, err := cl.Do("TRASH " + args[0])
		if err != nil {
			die(err)
		}
		lines, err := r.Lines(int(r.Int(0)))
		r.Close()
		if err != nil {
			die(err)
		}
		for _, l := range lines {
			// <name> <kind> <at> <by> <index> <name it had> <path>\t<title>
			head, title, _ := strings.Cut(l, "\t")
			f := strings.Fields(head)
			if len(f) != 7 {
				fmt.Println(toTerm([]byte(l)))
				continue
			}
			var at int64
			fmt.Sscan(f[2], &at)
			from := f[6] + "/" + f[5]
			if f[6] == "." {
				from = f[5]
			}
			fmt.Printf("%-22s %-5s %s %-12s #%-4s %-24s %s\n", f[0], f[1],
				time.Unix(at, 0).Format("2006-01-02 15:04"), f[3], f[4], from, toTerm([]byte(title)))
		}

	case "restore":
		need(2)
		r, err := cl.Do("RESTORE " + args[0] + " " + args[1])
		if err != nil {
			die(err)
		}
		lines, err := r.Lines(1)
		r.Close()
		if err != nil {
			die(err)
		}
		fmt.Printf("restored to %s/%s (#%d)\n", lines[0], r.Words[1], r.Int(0))

	case "purge":
		if len(args) == 2 && !strings.HasPrefix(args[0], "-") {
			if _, err := cl.Simple("PURGE " + args[0] + " " + args[1]); err != nil {
				die(err)
			}
			fmt.Println("purged 1")
			break
		}
		fs := flag.NewFlagSet("purge", flag.ExitOnError)
		days := fs.Int("days", 30, "Remove what was deleted more than this many days ago")
		fs.Parse(args)
		w, err := cl.Simple(fmt.Sprintf("PURGE %d", *days))
		if err != nil {
			die(err)
		}
		fmt.Printf("purged %s\n", w[0])

	case "expire":
		fs := flag.NewFlagSet("expire", flag.ExitOnError)
		days := fs.Int("days", 5, "Remove the files older than this many days")
		fs.Parse(args)
		if fs.NArg() != 2 {
			flag.Usage()
			os.Exit(2)
		}
		w, err := cl.Simple(fmt.Sprintf("EXPIRE %s %s %d", fs.Arg(0), fs.Arg(1), *days))
		if err != nil {
			die(err)
		}
		fmt.Printf("expired %s\n", w[0])

	case "rename-root":
		need(2)
		if _, err := cl.Simple("RENAME_ROOT " + args[0] + " " + args[1]); err != nil {
			die(err)
		}

	case "delete-root":
		need(1)
		if _, err := cl.Simple("DELETE_ROOT " + args[0]); err != nil {
			die(err)
		}

	case "index":
		need(1)
		r, err := cl.Do("INDEX " + args[0])
		if err != nil {
			die(err)
		}
		text := make([]byte, r.Int(0))
		_, err = io.ReadFull(r.R, text)
		r.Close()
		if err != nil {
			die(err)
		}
		os.Stdout.WriteString(toTerm(text))

	case "verify":
		line := "VERIFY"
		if len(args) > 0 && (args[0] == "-fix" || args[0] == "fix") {
			line += " fix"
		}
		r, err := cl.Do(line)
		if err != nil {
			die(err)
		}
		problems, err := r.Lines(int(r.Int(6)))
		r.Close()
		if err != nil {
			die(err)
		}
		fmt.Printf("listings=%d blobs=%d entries=%d stored_bytes=%d pending=%d\n", r.Int(1), r.Int(2), r.Int(3), r.Int(4), r.Int(5))
		for _, p := range problems {
			fmt.Println("  " + p)
		}
		if r.Int(0) != 1 {
			fmt.Println("PROBLEMS FOUND")
			os.Exit(1)
		}
		fmt.Println("OK")

	case "metrics":
		r, err := cl.Do("METRICS")
		if err != nil {
			die(err)
		}
		io.CopyN(os.Stdout, r.R, r.Int(0))
		r.Close()

	case "pending":
		w, err := cl.Simple("PENDING")
		if err != nil {
			die(err)
		}
		fmt.Println(w[0])

	case "daily-backup":
		os.Exit(dailyBackup(args))

	case "sharing":
		need(1)
		if err := sharing(args[0]); err != nil {
			die(err)
		}

	case "verify-backup":
		need(1)
		ok, err := verifyBackup(args[0])
		if err != nil {
			die(err)
		}
		if !ok {
			fmt.Println("PROBLEMS FOUND")
			os.Exit(1)
		}
		fmt.Println("OK")

	case "backup":
		line := "BACKUP"
		if len(args) > 0 {
			line += " " + args[0]
		}
		w, err := cl.Simple(line)
		if err != nil {
			die(err)
		}
		fmt.Println(strings.Join(w, " "))

	default:
		flag.Usage()
		os.Exit(2)
	}
}

// isDir tells whether path is a directory, following symbolic links.
func isDir(path string) bool {
	fi, err := os.Stat(path)
	return err == nil && fi.IsDir()
}

// resolveLinks runs RESOLVE_LINKS (oldHomes: none for gem.svc's default)
// and prints what it did.
func resolveLinks(oldHomes []string) int {
	line := "RESOLVE_LINKS"
	for _, h := range oldHomes {
		abs, err := filepath.Abs(h)
		if err != nil {
			die(err)
		}
		line += " " + abs
	}
	r, err := cl.Do(line)
	if err != nil {
		fmt.Fprintf(os.Stderr, "resolve-links: %v\n", err)
		return 1
	}
	problems, err := r.Lines(int(r.Int(5)))
	r.Close()
	if err != nil {
		die(err)
	}
	for _, p := range problems {
		fmt.Println("  " + toTerm([]byte(p)))
	}
	fmt.Printf("links resolved: dirs=%d files=%d indexes=%d broken=%d loops=%d (now missing)\n",
		r.Int(0), r.Int(1), r.Int(2), r.Int(3), r.Int(4))
	return 0
}

// importAll imports the site, every board and, with users, every mail
// folder, jobs at a time.
func importAll(bbsHome string, jobs int, replace, users bool) int {
	var boards []string
	letters, err := os.ReadDir(filepath.Join(bbsHome, "man", "boards"))
	if err != nil {
		die(err)
	}
	for _, l := range letters {
		// man/boards/<c> and the boards in it may be symbolic links to
		// other disks: followed.
		if !isDir(filepath.Join(bbsHome, "man", "boards", l.Name())) {
			continue
		}
		des, err := os.ReadDir(filepath.Join(bbsHome, "man", "boards", l.Name()))
		if err != nil {
			fmt.Fprintf(os.Stderr, "%v\n", err)
			continue
		}
		for _, d := range des {
			if storage.ValidName(d.Name()) && strings.EqualFold(d.Name()[:1], l.Name()) &&
				isDir(filepath.Join(bbsHome, "man", "boards", l.Name(), d.Name())) {
				boards = append(boards, d.Name())
			}
		}
	}
	sort.Strings(boards)
	// Board names are case-insensitive (getbnum(3)): a second directory
	// differing only in case is left over from a rename and not imported.
	seen := map[string]string{}
	uniq := boards[:0]
	for _, b := range boards {
		if first, ok := seen[strings.ToLower(b)]; ok {
			fmt.Printf("%s: not imported, same board as %s (names are case-insensitive)\n", b, first)
			continue
		}
		seen[strings.ToLower(b)] = b
		uniq = append(uniq, b)
	}
	boards = uniq
	roots := []string{storage.RootSite}
	for _, b := range boards {
		roots = append(roots, storage.BoardRoot(b))
	}
	if users {
		homes, _ := filepath.Glob(filepath.Join(bbsHome, "home", "*", "*", "man"))
		sort.Strings(homes)
		for _, h := range homes {
			id := filepath.Base(filepath.Dir(h))
			if fi, err := os.Stat(h); err == nil && fi.IsDir() && storage.ValidName(id) {
				roots = append(roots, storage.UserRoot(id))
			}
		}
	}
	boards = roots
	fmt.Printf("%d gem areas\n", len(boards))
	var done, failed, skipped atomic.Int64
	start := time.Now()
	ch := make(chan string)
	var wg sync.WaitGroup
	var outMu sync.Mutex
	for i := 0; i < max(1, jobs); i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for b := range ch {
				line := "IMPORT " + b
				if replace {
					line += " replace"
				}
				w, err := cl.Simple(line)
				n := done.Add(1)
				outMu.Lock()
				var ce *client.Error
				switch {
				case errors.As(err, &ce) && ce.Code() == storage.ErrRootExists.Code:
					skipped.Add(1)
				case err != nil:
					failed.Add(1)
					fmt.Printf("[%d/%d] %s: %v\n", n, len(boards), b, err)
				default:
					fmt.Printf("[%d/%d] %s: %s (%sms)\n", n, len(boards), b, strings.Join(w[1:], " "), w[0])
				}
				outMu.Unlock()
			}
		}()
	}
	for _, b := range boards {
		ch <- b
	}
	close(ch)
	wg.Wait()
	fmt.Printf("done in %v: %d gem areas, %d skipped (already there), %d failed\n",
		time.Since(start).Round(time.Second), len(boards), skipped.Load(), failed.Load())
	if failed.Load() > 0 {
		return 1
	}
	return 0
}
