package daemon

import (
	"bufio"
	"bytes"
	"os"
	"strings"
)

// ConfigEntry is a "key = value" line of mail.svc.conf.
type ConfigEntry struct {
	Key, Value string
}

// ParseConfig reads mail.svc.conf, like the other *.svc.conf: one
// "key = value" (or "key value") per line; keys are in lower case with
// '_' turned into '-', and may repeat. Empty lines and lines starting with
// '#' or ';' are skipped.
func ParseConfig(path string) ([]ConfigEntry, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var entries []ConfigEntry
	sc := bufio.NewScanner(bytes.NewReader(b))
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || line[0] == '#' || line[0] == ';' {
			continue
		}
		var key, val string
		if i := strings.IndexByte(line, '='); i >= 0 {
			key, val = line[:i], line[i+1:]
		} else if f := strings.Fields(line); len(f) > 0 {
			key, val = f[0], strings.Join(f[1:], " ")
		}
		key = strings.ReplaceAll(strings.ToLower(strings.TrimSpace(key)), "_", "-")
		val = strings.Trim(strings.TrimSpace(val), `"'`)
		if key == "" {
			continue
		}
		entries = append(entries, ConfigEntry{key, val})
	}
	return entries, sc.Err()
}
