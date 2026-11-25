// Command rotatelog rotates and archives the logs of the BBS as configured
// in $BBSHOME/etc/rotatelog.conf (see sample/etc/rotatelog.conf and package
// pttbbs/rotatelog/rotate). The crontab runs it once a day, a little after
// midnight.
//
//	rotatelog [-c conf] [-n] [run]      rotate the entries that are due
//	rotatelog [-c conf] [-n] force <path|name>...
//	                                    rotate these now, due or not
//	rotatelog [-c conf] status          list the entries, when last and next
//	rotatelog [-c conf] check           check the configuration
package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"pttbbs/bbs"
	"pttbbs/rotatelog/rotate"
)

func usage() {
	fmt.Fprintf(os.Stderr, `usage: rotatelog [options] [run | force <path|name>... | status | check]

  run     rotate the entries that are due (default)
  force   rotate these entries now, due or not
  status  list the entries, when they were last rotated and when next
  check   check the configuration

options:
`)
	flag.PrintDefaults()
}

func main() {
	home := bbs.BBSHome()
	conf := flag.String("c", "", "configuration (default: $BBSHOME/etc/rotatelog.conf)")
	dry := flag.Bool("n", false, "dry run: tell what would be done")
	nowFlag := flag.String("now", "", "pretend it is this time (2006-01-02T15:04), for tests")
	flag.Usage = usage
	flag.Parse()
	if *conf == "" {
		*conf = filepath.Join(home, "etc", "rotatelog.conf")
	}
	now := time.Now()
	if *nowFlag != "" {
		t, err := time.ParseInLocation("2006-01-02T15:04", *nowFlag, time.Local)
		if err != nil {
			fmt.Fprintf(os.Stderr, "rotatelog: -now: %v\n", err)
			os.Exit(2)
		}
		now = t
	}

	cfg, found, err := rotate.LoadConfig(*conf)
	if err != nil {
		fmt.Fprintf(os.Stderr, "rotatelog: %v\n", err)
		os.Exit(2)
	}
	cmd, args := "run", flag.Args()
	if len(args) > 0 {
		cmd, args = args[0], args[1:]
	}

	switch cmd {
	case "check":
		src := *conf
		if !found {
			src = "built-in defaults (no " + *conf + ")"
		}
		fmt.Printf("%s: %d entries, OK\n", src, len(cfg.Entries))
		return
	case "status":
		state, err := rotate.LoadState(home)
		if err != nil {
			fmt.Fprintf(os.Stderr, "rotatelog: %v\n", err)
			os.Exit(1)
		}
		r := &rotate.Rotator{Home: home, Config: cfg, State: state, Now: now}
		fmt.Printf("%-28s %-12s %-30s %-10s %-10s %s\n", "PATH", "WHEN", "WHAT",
			"LAST", "NEXT", "COMPRESS")
		for _, e := range cfg.Entries {
			last := "-"
			if st := state[e.Key()]; st != nil && st.Last != "" {
				last = st.Last
			}
			next := rotate.Next(e, now).Format("2006-01-02")
			if r.Due(e) {
				next = "now"
			}
			opt := e.Compression.String()
			left, _ := filepath.Glob(filepath.Join(home, e.Path) + ".*.rotating")
			if e.Kind == rotate.Archive && len(left) > 0 {
				opt += fmt.Sprintf(" (%d not archived yet: %s.*.rotating)",
					len(left), e.Path)
			}
			if e.CopyTruncate {
				opt += " copytruncate"
			}
			fmt.Printf("%-28s %-12s %-30s %-10s %-10s %s\n", e.Path, e.When(),
				e.What(), last, next, opt)
		}
		return
	case "run", "force":
	default:
		usage()
		os.Exit(2)
	}

	var force map[string]bool
	if cmd == "force" {
		if len(args) == 0 {
			usage()
			os.Exit(2)
		}
		force = map[string]bool{}
		for _, a := range args {
			ok := false
			for _, e := range cfg.Entries {
				if a == e.Path || a == e.Name {
					ok = true
				}
			}
			if !ok {
				fmt.Fprintf(os.Stderr, "rotatelog: %s is not in the configuration\n", a)
				os.Exit(2)
			}
			force[a] = true
		}
	} else if len(args) > 0 {
		usage()
		os.Exit(2)
	}

	lock, err := rotate.Lock(home)
	if err != nil {
		fmt.Fprintf(os.Stderr, "rotatelog: %v\n", err)
		os.Exit(1)
	}
	defer lock.Close()
	state, err := rotate.LoadState(home)
	if err != nil {
		fmt.Fprintf(os.Stderr, "rotatelog: %v\n", err)
		os.Exit(1)
	}
	r := &rotate.Rotator{Home: home, Config: cfg, State: state, Now: now,
		DryRun: *dry, Out: os.Stdout}
	errs := r.Run(force)
	if !*dry {
		if err := rotate.SaveState(home, state); err != nil {
			errs = append(errs, err)
		}
	}
	for _, err := range errs {
		fmt.Fprintf(os.Stderr, "rotatelog: %v\n", err)
	}
	if len(errs) > 0 {
		os.Exit(1)
	}
}
