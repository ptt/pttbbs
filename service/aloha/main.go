package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"runtime/debug"
	"strconv"
	"strings"
	"syscall"
	"time"

	"pttbbs/aloha/daemon"
	"pttbbs/bbs"
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

func preprocessArgs(args []string) []string {
	var res []string
	for _, arg := range args {
		if strings.HasPrefix(arg, "-") && !strings.HasPrefix(arg, "--") && len(arg) > 2 {
			allV := true
			for _, r := range arg[1:] {
				if r != 'v' && r != 'V' {
					allV = false
					break
				}
			}
			if allV {
				for i := 0; i < len(arg)-1; i++ {
					res = append(res, "-v")
				}
				continue
			}
		}
		res = append(res, arg)
	}
	return res
}

func main() {
	bbsHome := os.Getenv("BBSHOME")
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}

	var verbose verboseValue

	logPath := flag.String("log", "", "Path to log file (default: $BBSHOME/log/aloha.svc.log)")
	debugMode := flag.Bool("D", false, "Enable debug mode (log directly to stdout)")
	flag.BoolVar(debugMode, "debug", false, "Enable debug mode (alias for -D)")
	daemonize := flag.Bool("d", true, "Daemonize mode (fork and exit 0 after setup ok)")
	maxProcs := flag.Int("maxprocs", 2, "GOMAXPROCS limit (default: 2)")
	maxThreads := flag.Int("maxthreads", 1000, "Max OS threads limit (default: 1000)")
	gcPercent := flag.Int("gcpercent", 50, "GC percent target (default: 50)")
	reconcileInterval := flag.Duration("reconcile-interval", 1*time.Hour, "Interval for periodic session reconciliation (default: 1h). Set to 0 to disable.")
	flag.DurationVar(reconcileInterval, "reconcile", 1*time.Hour, "Alias for -reconcile-interval")
	flag.Var(&verbose, "v", "Verbose mode (can be specified multiple times, e.g. -v -v or -vv)")
	flag.Var(&verbose, "verbose", "Alias for -v")

	os.Args = append([]string{os.Args[0]}, preprocessArgs(os.Args[1:])...)
	flag.Parse()

	if *debugMode {
		*daemonize = false
		verbose += 10
	}

	socketPath := filepath.Join(bbsHome, "run", "aloha.svc.sock")

	if *daemonize {
		if daemon.IsSocketOccupied(socketPath) {
			fmt.Fprintf(os.Stderr, "[aloha.svc] Error: UNIX domain socket %s is already occupied by another instance\n", socketPath)
			os.Exit(1)
		}

		if err := forkDaemon(); err != nil {
			fmt.Fprintf(os.Stderr, "[aloha.svc] Failed to daemonize: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("[aloha.svc] Starting PTT BBS Aloha Service in background...\n")
		os.Exit(0)
	}

	if *logPath == "" {
		*logPath = filepath.Join(bbsHome, "log", "aloha.svc.log")
	}

	if *maxProcs > 0 {
		runtime.GOMAXPROCS(*maxProcs)
	}
	if *maxThreads > 0 {
		debug.SetMaxThreads(*maxThreads)
	}
	if *gcPercent > 0 {
		debug.SetGCPercent(*gcPercent)
	}

	log.SetFlags(log.LstdFlags)

	// Print startup banner so stdout receives startup feedback when running directly or in debug
	fmt.Printf("[aloha.svc] Starting PTT BBS Aloha Service...\n")
	fmt.Printf("[aloha.svc] BBSHOME: %s, Log: %s\n", bbsHome, *logPath)
	fmt.Printf("[aloha.svc] Performance settings: GOMAXPROCS=%d, MaxThreads=%d, GCPercent=%d, ReconcileInterval=%v, Verbose=%d\n", runtime.GOMAXPROCS(0), *maxThreads, *gcPercent, *reconcileInterval, int(verbose))

	if *debugMode {
		log.SetOutput(os.Stdout)
		log.Printf("[aloha.svc] Debug mode enabled (-D/-debug): logging directly to stdout (Verbose=%d)", int(verbose))
	} else {
		// Configure logger to write to log file for all subsequent operational logs
		if err := os.MkdirAll(filepath.Dir(*logPath), 0755); err == nil {
			logFile, err := os.OpenFile(*logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
			if err == nil {
				log.SetOutput(logFile)
				// Write startup banner to log file as well
				log.Printf("[aloha.svc] Starting PTT BBS Aloha Service...")
				log.Printf("[aloha.svc] BBSHOME: %s, Log: %s", bbsHome, *logPath)
				log.Printf("[aloha.svc] Performance settings: GOMAXPROCS=%d, MaxThreads=%d, GCPercent=%d, ReconcileInterval=%v, Verbose=%d", runtime.GOMAXPROCS(0), *maxThreads, *gcPercent, *reconcileInterval, int(verbose))
			} else {
				log.Printf("[aloha.svc] Warning: failed to open log file %s: %v", *logPath, err)
			}
		}
	}

	service, err := daemon.NewService(bbsHome, socketPath)
	if err != nil {
		log.Fatalf("[aloha.svc] Failed to initialize Aloha Service: %v", err)
	}

	service.SetVerbose(int(verbose))
	service.SetReconcileInterval(*reconcileInterval)

	if err := service.Start(); err != nil {
		log.Fatalf("[aloha.svc] Service stopped with error: %v", err)
	}
}

func forkDaemon() error {
	execPath, err := os.Executable()
	if err != nil {
		execPath = os.Args[0]
	}

	var args []string
	for i := 1; i < len(os.Args); i++ {
		arg := os.Args[i]
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
