package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"syscall"

	"pttbbs/bbs"
	"pttbbs/ipc/daemon"
)

type verboseValue int

func (v *verboseValue) String() string {
	return fmt.Sprintf("%d", int(*v))
}

func (v *verboseValue) Set(s string) error {
	if s == "true" {
		*v++
		return nil
	}
	if s == "false" {
		return nil
	}
	if n, err := strconv.Atoi(s); err == nil {
		*v = verboseValue(n)
		return nil
	}
	count := 0
	for _, r := range s {
		if r == 'v' || r == 'V' {
			count++
		} else {
			return fmt.Errorf("invalid verbose value: %s", s)
		}
	}
	if count > 0 {
		*v += verboseValue(count)
		return nil
	}
	return nil
}

func (v *verboseValue) IsBoolFlag() bool {
	return true
}

func forkDaemon(cliArgs []string) error {
	execPath, err := os.Executable()
	if err != nil {
		execPath = os.Args[0]
	}

	var args []string
	for _, arg := range cliArgs {
		if arg == "-d" || strings.HasPrefix(arg, "-d=") || arg == "--d" || strings.HasPrefix(arg, "--d=") {
			continue
		}
		args = append(args, arg)
	}
	args = append(args, "-d=false")

	cmd := exec.Command(execPath, args...)
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Setsid: true,
	}
	cmd.Stdin = nil
	cmd.Stdout = nil
	cmd.Stderr = nil

	return cmd.Start()
}

func raiseFDLimit(target uint64) {
	if target <= 0 {
		return
	}
	var rlim syscall.Rlimit
	if err := syscall.Getrlimit(syscall.RLIMIT_NOFILE, &rlim); err != nil {
		log.Printf("[ipc.svc] Warning: failed to query RLIMIT_NOFILE: %v", err)
		return
	}
	oldCur := rlim.Cur

	if rlim.Cur >= target {
		log.Printf("[ipc.svc] RLIMIT_NOFILE is already %d (>= %d, max: %d)", rlim.Cur, target, rlim.Max)
		return
	}

	newRlim := rlim
	newRlim.Cur = target
	if newRlim.Max < target {
		newRlim.Max = target
	}

	if err := syscall.Setrlimit(syscall.RLIMIT_NOFILE, &newRlim); err != nil {
		// If increasing Max failed (e.g. unprivileged), try raising Cur to current Max.
		if rlim.Cur < rlim.Max {
			newRlim.Cur = rlim.Max
			newRlim.Max = rlim.Max
			if err2 := syscall.Setrlimit(syscall.RLIMIT_NOFILE, &newRlim); err2 != nil {
				log.Printf("[ipc.svc] Warning: failed to raise RLIMIT_NOFILE to max %d: %v", rlim.Max, err2)
			}
		}
	}

	_ = syscall.Getrlimit(syscall.RLIMIT_NOFILE, &rlim)
	log.Printf("[ipc.svc] Adjusted RLIMIT_NOFILE from %d to %d (max: %d, requested: %d)", oldCur, rlim.Cur, rlim.Max, target)
	if rlim.Cur < target {
		log.Printf("[ipc.svc] Warning: RLIMIT_NOFILE (%d) is lower than requested %d. Run 'ulimit -n %d' before launching ipc.svc.", rlim.Cur, target, target)
	}
}

func getCurrentFDLimit() uint64 {
	var rlim syscall.Rlimit
	if err := syscall.Getrlimit(syscall.RLIMIT_NOFILE, &rlim); err != nil {
		return 0
	}
	return rlim.Cur
}

func main() {
	bbsHome := os.Getenv("BBSHOME")
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}

	var verbose verboseValue

	logPath := flag.String("log", "", "Path to log file (default: $BBSHOME/log/ipc.svc.log)")
	debugMode := flag.Bool("D", false, "Enable debug mode (log directly to stdout)")
	flag.BoolVar(debugMode, "debug", false, "Enable debug mode (alias for -D)")
	daemonize := flag.Bool("d", true, "Daemonize mode (fork and exit 0 after setup ok)")
	nofileLimit := flag.Int("nofile", 100000, "RLIMIT_NOFILE open files limit (default: 100000)")
	flag.IntVar(nofileLimit, "rlimit-nofile", 100000, "Alias for -nofile")
	maxProcs := flag.Int("maxprocs", 0, "GOMAXPROCS limit (0 or default: CPU core count)")
	flag.Var(&verbose, "v", "Verbose mode (can be specified multiple times, e.g. -v -v or -vv)")
	flag.Var(&verbose, "verbose", "Alias for -v")

	cliArgs := os.Args[1:]
	flag.Parse()

	if *debugMode {
		*daemonize = false
		verbose += 10
	}

	socketPath := filepath.Join(bbsHome, "run", "ipc.svc.sock")

	if *daemonize {
		if daemon.IsSocketOccupied(socketPath) {
			fmt.Fprintf(os.Stderr, "[ipc.svc] Error: UNIX domain socket %s is already occupied by another instance\n", socketPath)
			os.Exit(1)
		}

		if err := forkDaemon(cliArgs); err != nil {
			fmt.Fprintf(os.Stderr, "[ipc.svc] Failed to daemonize: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("[ipc.svc] Starting PTT BBS IPC Service in background...\n")
		os.Exit(0)
	}

	if *logPath == "" {
		*logPath = filepath.Join(bbsHome, "log", "ipc.svc.log")
	}

	procs := *maxProcs
	if procs <= 0 {
		procs = runtime.NumCPU()
	}
	runtime.GOMAXPROCS(procs)

	if !*debugMode {
		_ = os.MkdirAll(filepath.Dir(*logPath), 0755)
		f, err := os.OpenFile(*logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
		if err != nil {
			log.Fatalf("[ipc.svc] Failed to open log file %s: %v", *logPath, err)
		}
		defer f.Close()
		log.SetOutput(f)
	}

	if *nofileLimit > 0 {
		raiseFDLimit(uint64(*nofileLimit))
	}

	log.Printf("[ipc.svc] Starting PTT BBS IPC Service...")
	log.Printf("[ipc.svc] BBSHOME: %s, Socket: %s, Log: %s", bbsHome, socketPath, *logPath)
	log.Printf("[ipc.svc] Performance settings: GOMAXPROCS=%d (cores=%d), RLIMIT_NOFILE=%d", runtime.GOMAXPROCS(0), runtime.NumCPU(), getCurrentFDLimit())

	_ = os.MkdirAll(filepath.Dir(socketPath), 0755)

	svc, err := daemon.NewService(socketPath, daemon.WithVerbose(int(verbose)))
	if err != nil {
		log.Fatalf("[ipc.svc] Failed to create IPC Service: %v", err)
	}

	if err := svc.Start(); err != nil {
		log.Fatalf("[ipc.svc] Failed to start IPC Service: %v", err)
	}

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	sig := <-sigChan
	log.Printf("[ipc.svc] Received signal %v, shutting down...", sig)

	svc.Stop()
	log.Printf("[ipc.svc] Service exited successfully")
}
