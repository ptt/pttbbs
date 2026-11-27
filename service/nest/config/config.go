package config

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// EngineLocation defines physical storage locations for an engine shard
type EngineLocation struct {
	Index    int    `json:"index"`
	DataDir  string `json:"data_dir"`
	CacheDir string `json:"cache_dir"`
}

// ServiceConfig holds the parsed nest.svc configuration
type ServiceConfig struct {
	ConfigFile     string `json:"config_file,omitempty"`
	BBSHome        string `json:"bbshome"`
	UnixSocket     string `json:"unix_socket"`
	LogPath        string `json:"log_path"`
	CacheSizeMB    int    `json:"cache_size_mb"`
	FlushSeconds   int    `json:"flush_seconds"`
	MinReplyRunes  int    `json:"min_reply_runes"`
	FilterEncoding string `json:"filter_encoding"`
	MaxOpenFiles   int    `json:"max_open_files"`
	// MaxPostCommentLines limits the lines of the comments of a post in
	// all (0: no limit); 51,200 by default, about what 5 MiB held before.
	MaxPostCommentLines int `json:"max_post_comment_lines"`
	ImportWorkers       int `json:"import_workers"`
	// Crosspost limits (see storage/crosspostlimit.go): within a rolling
	// window, how many boards a person may crosspost an original to, and
	// how often it may be crossposted in all (0: no limit); originals in
	// the exempt boards are not limited.
	CrosspostWindowSec    int      `json:"crosspost_window_sec"`
	CrosspostPersonMax    int      `json:"crosspost_person_max"`
	CrosspostPostMax      int      `json:"crosspost_post_max"`
	CrosspostExemptBoards []string `json:"crosspost_exempt_boards,omitempty"`
	// SiteURL (e.g. https://www.ptt.cc) makes the ActivityPub IDs (ap_id,
	// actor_id) in the Lemmy API.
	SiteURL string `json:"site_url,omitempty"`
	// Engines lists the shards. The community registry (communities.db,
	// shared by all shards) is kept in the DataDir of the first one.
	Engines []EngineLocation `json:"engines"`
	// Pins chooses the shard of communities (by case-insensitive name)
	// when they are created; other new communities go to the shard with
	// the least disk usage. The registry then keeps the shard of each
	// community, so changing the pins does not move existing communities,
	// and renaming one needs no change.
	Pins map[string]int `json:"pins"`
}

func (c *ServiceConfig) IsBig5() bool {
	enc := strings.ToLower(strings.TrimSpace(c.FilterEncoding))
	return enc == "" || enc == "big5" || enc == "big5-uao" || enc == "uao"
}

// DefaultMaxPostCommentLines: the busiest PTT posts stopped at 5 MiB,
// about 51,300 lines of comments.
const DefaultMaxPostCommentLines = 51200

// DataDir returns where nest.svc keeps its databases by default (its Pebble
// DBs, SQLite and the community registry): $BBSHOME/db/nest.db, as every
// service keeps its own in $BBSHOME/db/<service>.db.
func DataDir(bbshome string) string {
	if bbshome == "" {
		bbshome = "/home/bbs"
	}
	return filepath.Join(bbshome, "db", "nest.db")
}

// DefaultConfig returns standard default configuration for single-engine mode
func DefaultConfig(bbshome string) *ServiceConfig {
	if bbshome == "" {
		bbshome = "/home/bbs"
	}
	return &ServiceConfig{
		BBSHome:        bbshome,
		UnixSocket:     filepath.Join(bbshome, "run", "nest.svc.sock"),
		LogPath:        filepath.Join(bbshome, "log", "nest.svc.log"),
		CacheSizeMB:    256,
		FlushSeconds:   2,
		MinReplyRunes:  20,
		FilterEncoding: "big5",
		ImportWorkers:  8,

		MaxPostCommentLines: DefaultMaxPostCommentLines,
		CrosspostWindowSec:  86400,
		CrosspostPersonMax:  5,
		CrosspostPostMax:    20,
		Engines: []EngineLocation{
			{
				Index:    0,
				DataDir:  DataDir(bbshome),
				CacheDir: "boards",
			},
		},
		Pins: make(map[string]int),
	}
}

// FindConfigFile searches for nest.svc.conf in standard PTT BBS paths
func FindConfigFile(cliPath string, bbshome string) string {
	if cliPath != "" {
		if fi, err := os.Stat(cliPath); err == nil && !fi.IsDir() {
			return cliPath
		}
		return ""
	}

	homeDir := os.Getenv("HOME")
	candidates := []string{
		filepath.Join(bbshome, "etc", "nest.svc.conf"),
		filepath.Join(homeDir, "etc", "nest.svc.conf"),
		"etc/nest.svc.conf",
		"nest.svc.conf",
	}

	for _, cand := range candidates {
		if cand == "" {
			continue
		}
		if fi, err := os.Stat(cand); err == nil && !fi.IsDir() {
			return cand
		}
	}
	return ""
}

// expandPath expands ~/ or $BBSHOME/ references
func expandPath(p string, bbshome string) string {
	p = strings.TrimSpace(p)
	if p == "" {
		return ""
	}
	if strings.HasPrefix(p, "~/") {
		p = filepath.Join(bbshome, p[2:])
	} else if strings.HasPrefix(p, "$BBSHOME/") {
		p = filepath.Join(bbshome, p[9:])
	}
	return filepath.Clean(p)
}

// LoadConfigFile loads and parses nest.svc.conf
func LoadConfigFile(path string, bbshome string) (*ServiceConfig, error) {
	cfg := DefaultConfig(bbshome)
	cfg.ConfigFile = path

	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var currentSection string
	var rawEngines []string
	var rawCaches []string

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") || strings.HasPrefix(line, ";") {
			continue
		}

		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			currentSection = strings.ToLower(strings.TrimSpace(line[1 : len(line)-1]))
			continue
		}

		// Parse key = value or key value
		var key, val string
		if eqIdx := strings.IndexByte(line, '='); eqIdx >= 0 {
			key = strings.TrimSpace(line[:eqIdx])
			val = strings.TrimSpace(line[eqIdx+1:])
		} else {
			parts := strings.Fields(line)
			if len(parts) >= 2 {
				key = parts[0]
				val = strings.Join(parts[1:], " ")
			} else {
				key = line
				val = ""
			}
		}

		keyLower := strings.ToLower(key)

		switch currentSection {
		case "engines", "engine":
			// Can be: "engine = /path", "0 = /path", or "/path"
			pathVal := expandPath(val, bbshome)
			if pathVal == "" && val == "" {
				pathVal = expandPath(key, bbshome)
			}
			if pathVal != "" {
				rawEngines = append(rawEngines, pathVal)
			}

		case "caches", "cache":
			pathVal := expandPath(val, bbshome)
			if pathVal == "" && val == "" {
				pathVal = expandPath(key, bbshome)
			}
			if pathVal != "" {
				rawCaches = append(rawCaches, pathVal)
			}

		case "pins", "pin":
			// Format: "Gossiping = 0" or "Gossiping 0"
			if idx, err := strconv.Atoi(val); err == nil {
				cfg.Pins[strings.TrimSpace(key)] = idx
			}

		default: // "global", "", or other sections
			switch keyLower {
			case "bbshome":
				cfg.BBSHome = expandPath(val, bbshome)
			case "unix_socket", "socket":
				cfg.UnixSocket = expandPath(val, bbshome)
			case "log_path", "log":
				cfg.LogPath = expandPath(val, bbshome)
			case "cache_size_mb", "cache_mb":
				if n, err := strconv.Atoi(val); err == nil && n > 0 {
					cfg.CacheSizeMB = n
				}
			case "flush_seconds":
				if n, err := strconv.Atoi(val); err == nil && n > 0 {
					cfg.FlushSeconds = n
				}
			case "min_reply_runes":
				if n, err := strconv.Atoi(val); err == nil && n >= 0 {
					cfg.MinReplyRunes = n
				}
			case "site_url":
				cfg.SiteURL = strings.TrimRight(strings.TrimSpace(val), "/")
			case "filter_encoding", "encoding":
				cfg.FilterEncoding = strings.TrimSpace(val)
			case "max_open_files", "max_files":
				if n, err := strconv.Atoi(val); err == nil && n > 0 {
					cfg.MaxOpenFiles = n
				}
			case "max_post_comment_lines":
				if n, err := strconv.Atoi(val); err == nil && n >= 0 {
					cfg.MaxPostCommentLines = n
				}
			case "crosspost_window_sec":
				if n, err := strconv.Atoi(val); err == nil && n >= 0 {
					cfg.CrosspostWindowSec = n
				}
			case "crosspost_person_max":
				if n, err := strconv.Atoi(val); err == nil && n >= 0 {
					cfg.CrosspostPersonMax = n
				}
			case "crosspost_post_max":
				if n, err := strconv.Atoi(val); err == nil && n >= 0 {
					cfg.CrosspostPostMax = n
				}
			case "crosspost_exempt_boards":
				cfg.CrosspostExemptBoards = nil
				for _, b := range strings.Split(val, ",") {
					if b = strings.TrimSpace(b); b != "" {
						cfg.CrosspostExemptBoards = append(cfg.CrosspostExemptBoards, b)
					}
				}
			case "workers", "import_workers":
				if n, err := strconv.Atoi(val); err == nil && n > 0 {
					cfg.ImportWorkers = n
				}
			case "engine":
				rawEngines = append(rawEngines, expandPath(val, bbshome))
			case "cache":
				rawCaches = append(rawCaches, expandPath(val, bbshome))
			case "pin":
				// pin Gossiping 0
				fields := strings.Fields(val)
				if len(fields) >= 2 {
					if idx, err := strconv.Atoi(fields[1]); err == nil {
						cfg.Pins[fields[0]] = idx
					}
				}
			}
		}
	}

	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("read config failed: %w", err)
	}

	// Build Engines list if specified in config
	if len(rawEngines) > 0 {
		cfg.Engines = make([]EngineLocation, len(rawEngines))
		for i, dataDir := range rawEngines {
			cacheDir := ""
			if i < len(rawCaches) {
				cacheDir = rawCaches[i]
			}
			if cacheDir == "" {
				cacheDir = "boards"
			}
			cfg.Engines[i] = EngineLocation{
				Index:    i,
				DataDir:  dataDir,
				CacheDir: cacheDir,
			}
		}
	}

	return cfg, nil
}
