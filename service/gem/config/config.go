// Package config reads gem.svc.conf.
package config

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"pttbbs/gem/storage"
)

// Config is the configuration of gem.svc.
type Config struct {
	DataDir    string // $BBSHOME/db/gem.db
	UnixSocket string // $BBSHOME/run/gem.svc.sock
	LogPath    string // $BBSHOME/log/gem.svc.log
	CacheMB    int
	Workers    int                       // readers per import
	Quota      storage.Limits            // of the gem areas of boards
	UserQuota  storage.Limits            // of the mail folders of people
	Boards     map[string]storage.Limits // lower(board): its own quotas
}

// DefaultUserLimits are the quotas of a person's mail folder.
var DefaultUserLimits = storage.Limits{
	Nodes: 20000,
	Dirs:  2000,
	Bytes: 200 << 20,
	Width: 5000,
	Depth: 12,
	Copy:  10000,
}

// DataDir is where gem.svc keeps its database: $BBSHOME/db/gem.db.
func DataDir(bbsHome string) string { return filepath.Join(bbsHome, "db", "gem.db") }

// PebbleDir is the Pebble database inside the data directory.
func PebbleDir(dataDir string) string { return filepath.Join(dataDir, "gem.pebble") }

// Socket is the default socket of gem.svc.
func Socket(bbsHome string) string { return filepath.Join(bbsHome, "run", "gem.svc.sock") }

// Default is the configuration without a gem.svc.conf.
func Default(bbsHome string) *Config {
	return &Config{
		DataDir:    DataDir(bbsHome),
		UnixSocket: Socket(bbsHome),
		LogPath:    filepath.Join(bbsHome, "log", "gem.svc.log"),
		CacheMB:    128,
		Workers:    8,
		Quota:      storage.DefaultLimits,
		UserQuota:  DefaultUserLimits,
		Boards:     map[string]storage.Limits{},
	}
}

// Path is where gem.svc.conf is: cli when given, else
// $BBSHOME/etc/gem.svc.conf ("" when there is none).
func Path(cli, bbsHome string) string {
	if cli != "" {
		return cli
	}
	p := filepath.Join(bbsHome, "etc", "gem.svc.conf")
	if _, err := os.Stat(p); err == nil {
		return p
	}
	return ""
}

// LimitsFor returns the quotas of the gem area root: of its board, of
// people, or none for the site.
func (c *Config) LimitsFor(root string) storage.Limits {
	kind, name, _ := storage.SplitRoot(root)
	switch kind {
	case "board":
		if l, ok := c.Boards[strings.ToLower(name)]; ok {
			return l
		}
		return c.Quota
	case "user":
		return c.UserQuota
	}
	return storage.Limits{}
}

func expand(p, bbsHome string) string {
	if p == "~" {
		return bbsHome
	}
	if strings.HasPrefix(p, "~/") {
		return filepath.Join(bbsHome, p[2:])
	}
	return p
}

// ParseSize reads 123, 10K, 5M, 1G (powers of 1024).
func ParseSize(s string) (int64, error) {
	s = strings.TrimSpace(strings.ToUpper(s))
	mult := int64(1)
	switch {
	case strings.HasSuffix(s, "K"):
		mult, s = 1<<10, s[:len(s)-1]
	case strings.HasSuffix(s, "M"):
		mult, s = 1<<20, s[:len(s)-1]
	case strings.HasSuffix(s, "G"):
		mult, s = 1<<30, s[:len(s)-1]
	}
	n, err := strconv.ParseInt(strings.TrimSpace(s), 10, 64)
	if err != nil || n < 0 {
		return 0, fmt.Errorf("bad size %q", s)
	}
	return n * mult, nil
}

func setLimit(l *storage.Limits, key, val string) (bool, error) {
	var dst *int64
	switch key {
	case "nodes":
		dst = &l.Nodes
	case "dirs":
		dst = &l.Dirs
	case "bytes":
		dst = &l.Bytes
	case "width":
		dst = &l.Width
	case "depth":
		dst = &l.Depth
	case "copy":
		dst = &l.Copy
	default:
		return false, nil
	}
	n, err := ParseSize(val)
	if err != nil {
		return true, err
	}
	*dst = n
	return true, nil
}

// Load reads path (may be "": defaults only).
//
//	[global]
//	cache_size_mb = 128
//	[quota]
//	nodes = 100000
//	bytes = 1G
//	[board C_Chat]      # starts from [quota]; 0 is no limit
//	nodes = 0
//	[user_quota]        # mail folders of people
//	nodes = 20000
func Load(path, bbsHome string) (*Config, error) {
	c := Default(bbsHome)
	if path == "" {
		return c, nil
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	type pending struct {
		board string
		lines [][2]string
	}
	var boards []pending
	section := "global"
	sc := bufio.NewScanner(f)
	lineNo := 0
	for sc.Scan() {
		lineNo++
		line := strings.TrimSpace(sc.Text())
		if i := strings.IndexByte(line, '#'); i >= 0 {
			line = strings.TrimSpace(line[:i])
		}
		if line == "" {
			continue
		}
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			section = strings.TrimSpace(line[1 : len(line)-1])
			if f := strings.Fields(section); len(f) == 2 && strings.EqualFold(f[0], "board") {
				boards = append(boards, pending{board: f[1]})
				section = "board"
			} else {
				section = strings.ToLower(section)
			}
			continue
		}
		k, v, ok := strings.Cut(line, "=")
		if !ok {
			return nil, fmt.Errorf("%s:%d: no '='", path, lineNo)
		}
		k = strings.ToLower(strings.TrimSpace(k))
		v = strings.TrimSpace(v)
		switch section {
		case "global":
			switch k {
			case "cache_size_mb":
				c.CacheMB, err = strconv.Atoi(v)
			case "workers":
				c.Workers, err = strconv.Atoi(v)
			case "data_dir":
				c.DataDir = expand(v, bbsHome)
			case "unix_socket":
				c.UnixSocket = expand(v, bbsHome)
			case "log_path":
				c.LogPath = expand(v, bbsHome)
			default:
				err = fmt.Errorf("unknown key %q", k)
			}
		case "quota":
			var known bool
			if known, err = setLimit(&c.Quota, k, v); !known {
				err = fmt.Errorf("unknown key %q", k)
			}
		case "user_quota":
			var known bool
			if known, err = setLimit(&c.UserQuota, k, v); !known {
				err = fmt.Errorf("unknown key %q", k)
			}
		case "board":
			b := &boards[len(boards)-1]
			b.lines = append(b.lines, [2]string{k, v})
		default:
			err = fmt.Errorf("unknown section [%s]", section)
		}
		if err != nil {
			return nil, fmt.Errorf("%s:%d: %v", path, lineNo, err)
		}
	}
	if err := sc.Err(); err != nil {
		return nil, err
	}
	// Boards start from [quota], wherever it is in the file.
	for _, b := range boards {
		l := c.Quota
		for _, kv := range b.lines {
			known, err := setLimit(&l, kv[0], kv[1])
			if !known {
				err = fmt.Errorf("unknown key %q", kv[0])
			}
			if err != nil {
				return nil, fmt.Errorf("%s: [board %s]: %v", path, b.board, err)
			}
		}
		c.Boards[strings.ToLower(b.board)] = l
	}
	return c, nil
}
