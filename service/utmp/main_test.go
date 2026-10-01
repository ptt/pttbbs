package main

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestParseConfigFile(t *testing.T) {
	content := `
# Sample utmp config
interval = 1.3s
fix-interval = 60s
nice = -20
maxprocs = 2
maxthreads = 1000
gcpercent = 50
verbose = 1

# Flag styles
-d
-v
`
	tmpDir := t.TempDir()
	confFile := filepath.Join(tmpDir, "utmp.svc.conf")
	if err := os.WriteFile(confFile, []byte(content), 0644); err != nil {
		t.Fatalf("failed to write conf: %v", err)
	}

	args, err := parseConfigFile(confFile)
	if err != nil {
		t.Fatalf("parseConfigFile failed: %v", err)
	}

	expected := []string{
		"-interval=1.3s",
		"-fix-interval=60s",
		"-nice=-20",
		"-maxprocs=2",
		"-maxthreads=1000",
		"-gcpercent=50",
		"-verbose=1",
		"-d",
		"-v",
	}

	if !reflect.DeepEqual(args, expected) {
		t.Errorf("got %v, want %v", args, expected)
	}
}

func TestParseConfigFileDurationNormalization(t *testing.T) {
	content := `
# Legacy shmctl style integer durations
interval = 1300000
fix-interval = 60
`
	tmpDir := t.TempDir()
	confFile := filepath.Join(tmpDir, "utmp.svc.conf")
	if err := os.WriteFile(confFile, []byte(content), 0644); err != nil {
		t.Fatalf("failed to write conf: %v", err)
	}

	args, err := parseConfigFile(confFile)
	if err != nil {
		t.Fatalf("parseConfigFile failed: %v", err)
	}

	expected := []string{
		"-interval=1300000us",
		"-fix-interval=60s",
	}

	if !reflect.DeepEqual(args, expected) {
		t.Errorf("got %v, want %v", args, expected)
	}
}
