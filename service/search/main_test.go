package main

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestParseConfigFile(t *testing.T) {
	content := `
# Sample search config
max-entries = 8192
max-indices: 67108864
max_aid_entries = 131072
gcpercent 100
pprof-addr = 127.0.0.1:6060

# Flag styles
-v
-cache-ttl=2h
`
	tmpDir := t.TempDir()
	confFile := filepath.Join(tmpDir, "search.conf")
	if err := os.WriteFile(confFile, []byte(content), 0644); err != nil {
		t.Fatalf("failed to write conf: %v", err)
	}

	args, err := parseConfigFile(confFile)
	if err != nil {
		t.Fatalf("parseConfigFile failed: %v", err)
	}

	expected := []string{
		"-max-entries=8192",
		"-max-indices=67108864",
		"-max-aid-entries=131072",
		"-gcpercent=100",
		"-pprof-addr=127.0.0.1:6060",
		"-v",
		"-cache-ttl=2h",
	}

	if !reflect.DeepEqual(args, expected) {
		t.Errorf("got %v, want %v", args, expected)
	}
}
