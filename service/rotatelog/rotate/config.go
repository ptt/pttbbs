// Package rotate rotates and archives the logs of the BBS, as configured in
// etc/rotatelog.conf (see sample/etc/rotatelog.conf). The crontab runs
// bin/rotatelog once a day, a little after midnight; the logs to rotate on
// other days (weekly, monthly) are picked by date, and a state file
// remembers what was done, so that running it again the same day does
// nothing.
//
// Archives are named after the last day they cover; snapshots after the
// day they are taken.
//
// Two kinds of entries:
//
//   - archive: a log (usies, mailog, ...), compressed into
//     adm/YYYY/<name>/<name>-YYYYMMDD.<ext> and removed (<name>: the file
//     name, unless given), named after the last day it covers (the day of
//     its last line; lines written on the day of the rotation, after
//     midnight, count as the day before). A directory per year, so that old
//     years can be removed at once, and in it one per log. usies is moved
//     to log/usies by bin/account at 0:01, right after it counts the logins
//     of the day, and archived from there.
//   - snapshot: a directory (tmp): moved away, a new empty one made, and the
//     old one packed into adm/YYYY/<name>/<name>-YYYYMMDD.tar.<ext>, then
//     removed.
//
// The writers of the BBS open the log for each line (log_filef(),
// file_appendf()), so moving a log is enough: the next line goes to a new
// file. For a daemon that keeps its log open (e.g. its stderr), the option
// copytruncate copies the log and empties it instead, as logrotate does
// (lines written in between may be lost).
package rotate

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
	"time"
)

// Kind is what an entry does.
type Kind int

const (
	Archive Kind = iota
	Snapshot
)

func (k Kind) String() string {
	return [...]string{"archive", "snapshot"}[k]
}

// Period is how often an entry is rotated.
type Period int

const (
	Daily Period = iota
	Weekly
	Monthly
)

// Compression is how archives are compressed.
type Compression struct {
	Algo  string // "zstd", "gzip" or "none"
	Level int    // 0: the default of the algorithm
}

// Ext is the extension of the files it makes ("" for none).
func (c Compression) Ext() string {
	switch c.Algo {
	case "zstd":
		return ".zst"
	case "gzip":
		return ".gz"
	}
	return ""
}

func (c Compression) String() string {
	if c.Level == 0 || c.Algo == "none" {
		return c.Algo
	}
	return fmt.Sprintf("%s:%d", c.Algo, c.Level)
}

// DefaultCompression is zstd:9. A day of usies (83 MB) became 13 MB in 2 s,
// with zstd:19 11 MB in 4 s, with gzip 18 MB in 2 s (the times include
// reading the archive back to check it). The zstd encoder has 4 speeds,
// which the levels map to: 1-2 fastest, 3-5 default, 6-9 better, 10 and up
// best. zless and zgrep do not read .zst; zstdless, zstdgrep and zstdcat
// (in the zstd package) do. Choose gzip in the configuration to keep
// using them.
var DefaultCompression = Compression{Algo: "zstd", Level: 9}

// ParseCompression parses "zstd", "zstd:19", "gzip:6" or "none".
func ParseCompression(s string) (Compression, error) {
	algo, level, hasLevel := strings.Cut(s, ":")
	c := Compression{Algo: algo}
	max := 0
	switch algo {
	case "zstd":
		max = 22
	case "gzip":
		max = 9
	case "none":
		if hasLevel {
			return c, fmt.Errorf("compression none takes no level: %q", s)
		}
		return c, nil
	default:
		return c, fmt.Errorf("unknown compression %q (zstd, gzip or none)", algo)
	}
	if hasLevel {
		n, err := strconv.Atoi(level)
		if err != nil || n < 1 || n > max {
			return c, fmt.Errorf("bad level of %s (1 to %d): %q", algo, max, s)
		}
		c.Level = n
	} else if algo == "zstd" {
		c.Level = DefaultCompression.Level
	}
	return c, nil
}

// Entry is a line of the configuration.
type Entry struct {
	Path         string // relative to BBSHOME
	Period       Period
	Weekday      time.Weekday // for Weekly
	Kind         Kind
	Name         string // Archive, Snapshot: name of the archives
	CopyTruncate bool
	Compression  Compression
	Line         int
}

// Key names the entry in the state.
func (e *Entry) Key() string { return e.Path }

// When describes the period, as in the configuration.
func (e *Entry) When() string {
	switch e.Period {
	case Weekly:
		return "weekly:" + weekdayNames[e.Weekday]
	case Monthly:
		return "monthly"
	}
	return "daily"
}

// What describes the action, as in the configuration.
func (e *Entry) What() string {
	return e.Kind.String() + ":" + e.Name
}

// Config is the parsed etc/rotatelog.conf.
type Config struct {
	Compression Compression
	Weekday     time.Weekday
	Entries     []*Entry
}

var weekdayNames = []string{"sun", "mon", "tue", "wed", "thu", "fri", "sat"}

func parseWeekday(s string) (time.Weekday, error) {
	for i, n := range weekdayNames {
		if strings.EqualFold(s, n) {
			return time.Weekday(i), nil
		}
	}
	return 0, fmt.Errorf("unknown weekday %q (sun to sat)", s)
}

// DefaultConfig is used when there is no etc/rotatelog.conf: the logs that
// bin/account used to rotate itself (it still moves usies to log/usies at
// 0:01, right after counting it).
const DefaultConfig = `
log/usies      daily       archive
etc/mailog     daily       archive
etc/osong.log  daily       archive
tmp            weekly:sat  snapshot
`

// ParseConfig parses a configuration:
//
//	compress <zstd[:level] | gzip[:level] | none>
//	weekday <sun..sat>
//	<path> <daily | weekly[:<day>] | monthly> <archive[:name] | snapshot[:name]> [copytruncate] [compress=...]
//
// The global settings apply to the entries after them.
func ParseConfig(r io.Reader) (*Config, error) {
	cfg := &Config{Compression: DefaultCompression, Weekday: time.Sunday}
	seen := map[string]int{}
	names := map[string]int{}
	sc := bufio.NewScanner(r)
	for n := 1; sc.Scan(); n++ {
		line := sc.Text()
		if i := strings.IndexByte(line, '#'); i >= 0 {
			line = line[:i]
		}
		f := strings.Fields(line)
		if len(f) == 0 {
			continue
		}
		bad := func(format string, args ...any) error {
			return fmt.Errorf("line %d: %s", n, fmt.Sprintf(format, args...))
		}
		switch f[0] {
		case "compress":
			if len(f) != 2 {
				return nil, bad("usage: compress <zstd[:level] | gzip[:level] | none>")
			}
			c, err := ParseCompression(f[1])
			if err != nil {
				return nil, bad("%v", err)
			}
			cfg.Compression = c
			continue
		case "weekday":
			if len(f) != 2 {
				return nil, bad("usage: weekday <sun..sat>")
			}
			d, err := parseWeekday(f[1])
			if err != nil {
				return nil, bad("%v", err)
			}
			cfg.Weekday = d
			continue
		}
		if len(f) < 3 {
			return nil, bad("usage: <path> <daily|weekly[:day]|monthly> <archive[:name]|snapshot[:name]> [options]")
		}
		e := &Entry{Path: strings.TrimSuffix(f[0], "/"), Compression: cfg.Compression,
			Weekday: cfg.Weekday, Line: n}
		if e.Path == "" || strings.HasPrefix(e.Path, "/") ||
			strings.Contains("/"+e.Path+"/", "/../") {
			return nil, bad("path must be relative to BBSHOME: %q", f[0])
		}
		if prev, ok := seen[e.Path]; ok {
			return nil, bad("%s is already on line %d", e.Path, prev)
		}
		seen[e.Path] = n

		when, day, hasDay := strings.Cut(f[1], ":")
		switch when {
		case "daily":
			e.Period = Daily
		case "weekly":
			e.Period = Weekly
			if hasDay {
				d, err := parseWeekday(day)
				if err != nil {
					return nil, bad("%v", err)
				}
				e.Weekday = d
			}
		case "monthly":
			e.Period = Monthly
		default:
			return nil, bad("unknown period %q (daily, weekly[:day], monthly)", f[1])
		}
		if hasDay && when != "weekly" {
			return nil, bad("only weekly takes a day: %q", f[1])
		}

		kind, arg, hasArg := strings.Cut(f[2], ":")
		base := e.Path[strings.LastIndexByte(e.Path, '/')+1:]
		switch kind {
		case "archive", "snapshot":
			e.Kind = Archive
			if kind == "snapshot" {
				e.Kind = Snapshot
			}
			e.Name = base
			if hasArg {
				e.Name = arg
			}
			if e.Name == "" || strings.ContainsAny(e.Name, "/ ") || e.Name == "." ||
				e.Name == ".." {
				return nil, bad("bad name %q", e.Name)
			}
			if prev, ok := names[e.Name]; ok {
				return nil, bad("name %s is already used on line %d", e.Name, prev)
			}
			names[e.Name] = n
		default:
			return nil, bad("unknown action %q (archive[:name], snapshot[:name])", f[2])
		}

		for _, o := range f[3:] {
			switch {
			case o == "copytruncate":
				if e.Kind == Snapshot {
					return nil, bad("copytruncate is for files, not snapshot")
				}
				e.CopyTruncate = true
			case strings.HasPrefix(o, "compress="):
				c, err := ParseCompression(strings.TrimPrefix(o, "compress="))
				if err != nil {
					return nil, bad("%v", err)
				}
				e.Compression = c
			default:
				return nil, bad("unknown option %q (copytruncate, compress=...)", o)
			}
		}
		cfg.Entries = append(cfg.Entries, e)
	}
	if err := sc.Err(); err != nil {
		return nil, err
	}
	return cfg, nil
}

// LoadConfig reads the configuration from path, or DefaultConfig if it
// does not exist.
func LoadConfig(path string) (*Config, bool, error) {
	f, err := os.Open(path)
	if os.IsNotExist(err) {
		cfg, err := ParseConfig(strings.NewReader(DefaultConfig))
		return cfg, false, err
	}
	if err != nil {
		return nil, false, err
	}
	defer f.Close()
	cfg, err := ParseConfig(f)
	if err != nil {
		return nil, true, fmt.Errorf("%s: %w", path, err)
	}
	return cfg, true, nil
}
