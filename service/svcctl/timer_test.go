package svcctl

import (
	"bytes"
	"os"
	"strings"
	"testing"
	"time"
)

func TestTimerReport(t *testing.T) {
	var buf bytes.Buffer
	timerOut = &buf
	defer func() { timerOut, start = os.Stderr, time.Now() }()

	start = time.Now()
	report(0) // Quick: silent.
	if buf.Len() != 0 {
		t.Errorf("quick command reported: %q", buf.String())
	}

	start = time.Now().Add(-2 * LongRun)
	report(1)
	if got := buf.String(); strings.Count(got, "\n") != 1 ||
		!strings.Contains(got, ": failed (exit 1) after 2m0s") {
		t.Errorf("report: %q", got)
	}
}
