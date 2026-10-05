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

// ServiceConfig holds the parsed post.svc configuration
type ServiceConfig struct {
	ConfigFile     string           `json:"config_file,omitempty"`
	BBSHome        string           `json:"bbshome"`
	UnixSocket     string           `json:"unix_socket"`
	LogPath        string           `json:"log_path"`
	CacheSizeMB    int              `json:"cache_size_mb"`
	FlushSeconds   int              `json:"flush_seconds"`
	MinReplyRunes  int              `json:"min_reply_runes"`
	FilterEncoding string           `json:"filter_encoding"`
	MaxOpenFiles   int              `json:"max_open_files"`
	ImportWorkers  int              `json:"import_workers"`
	Engines        []EngineLocation `json:"engines"`
	Pins           map[string]int   `json:"pins"`
}

func (c *ServiceConfig) IsBig5() bool {
	enc := strings.ToLower(strings.TrimSpace(c.FilterEncoding))
	return enc == "" || enc == "big5" || enc == "big5-uao" || enc == "uao"
}

// DefaultConfig returns standard default configuration for single-engine mode
func DefaultConfig(bbshome string) *ServiceConfig {
	if bbshome == "" {
		bbshome = "/home/bbs"
	}
	return &ServiceConfig{
		BBSHome:        bbshome,
		UnixSocket:     filepath.Join(bbshome, "run", "post.svc.sock"),
		LogPath:        filepath.Join(bbshome, "log", "post.svc.log"),
		CacheSizeMB:    256,
		FlushSeconds:   2,
		MinReplyRunes:  20,
		FilterEncoding: "big5",
		ImportWorkers:  8,
		Engines: []EngineLocation{
			{
				Index:    0,
				DataDir:  filepath.Join(bbshome, "db"),
				CacheDir: "boards",
			},
		},
		Pins: make(map[string]int),
	}
}

// FindConfigFile searches for post.svc.conf in standard PTT BBS paths
func FindConfigFile(cliPath string, bbshome string) string {
	if cliPath != "" {
		if fi, err := os.Stat(cliPath); err == nil && !fi.IsDir() {
			return cliPath
		}
		return ""
	}

	homeDir := os.Getenv("HOME")
	candidates := []string{
		filepath.Join(bbshome, "etc", "post.svc.conf"),
		filepath.Join(homeDir, "etc", "post.svc.conf"),
		"etc/post.svc.conf",
		"post.svc.conf",
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

// LoadConfigFile loads and parses post.svc.conf
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
			case "filter_encoding", "encoding":
				cfg.FilterEncoding = strings.TrimSpace(val)
			case "max_open_files", "max_files":
				if n, err := strconv.Atoi(val); err == nil && n > 0 {
					cfg.MaxOpenFiles = n
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
