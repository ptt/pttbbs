package main

import (
	"runtime"
	"syscall"
	"testing"
)

func TestRaiseFDLimit(t *testing.T) {
	var rlim syscall.Rlimit
	if err := syscall.Getrlimit(syscall.RLIMIT_NOFILE, &rlim); err != nil {
		t.Skipf("Getrlimit not supported: %v", err)
	}

	target := uint64(100000)
	if rlim.Max > 0 && rlim.Max < target {
		target = rlim.Max
	}

	raiseFDLimit(target)

	cur := getCurrentFDLimit()
	if cur < target {
		t.Errorf("getCurrentFDLimit() = %d, expected at least %d", cur, target)
	}
}

func TestGOMAXPROCSConfiguration(t *testing.T) {
	orig := runtime.GOMAXPROCS(0)
	defer runtime.GOMAXPROCS(orig)

	// Test default / 0 uses NumCPU
	procs := 0
	if procs <= 0 {
		procs = runtime.NumCPU()
	}
	runtime.GOMAXPROCS(procs)
	if got := runtime.GOMAXPROCS(0); got != runtime.NumCPU() {
		t.Errorf("GOMAXPROCS got %d, want NumCPU %d", got, runtime.NumCPU())
	}

	// Test custom value
	custom := 3
	runtime.GOMAXPROCS(custom)
	if got := runtime.GOMAXPROCS(0); got != custom {
		t.Errorf("GOMAXPROCS got %d, want custom %d", got, custom)
	}
}
