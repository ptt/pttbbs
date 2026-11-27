package svcctl

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Every *.ctl command that runs for LongRun or more reports how long it
// took, so nobody has to run imports, renders or backups under time(1).
// A *.ctl exits only from main, by svcctl.Exit(run()).
const LongRun = time.Minute

var (
	start    = time.Now() // Set when the package is initialized.
	timerOut io.Writer = os.Stderr
)

func report(code int) {
	d := time.Since(start)
	if d < LongRun {
		return
	}
	cmd := strings.Join(append([]string{filepath.Base(os.Args[0])}, os.Args[1:]...), " ")
	d = d.Round(time.Second)
	if code == 0 {
		fmt.Fprintf(timerOut, "%s: took %v\n", cmd, d)
	} else {
		fmt.Fprintf(timerOut, "%s: failed (exit %d) after %v\n", cmd, code, d)
	}
}

// Exit reports the time taken, if long, and exits with code.
func Exit(code int) {
	report(code)
	os.Exit(code)
}
