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
	"strings"
	"syscall"
	"time"

	"pttbbs/aloha/daemon"
	"pttbbs/bbs"
)

func main() {
	bbsHome := os.Getenv("BBSHOME")
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}

	logPath := flag.String("log", "", "Path to log file (default: $BBSHOME/log/aloha.svc.log)")
	debugMode := flag.Bool("D", false, "Enable debug mode (log directly to stdout)")
	flag.BoolVar(debugMode, "debug", false, "Enable debug mode (alias for -D)")
	daemonize := flag.Bool("d", true, "Daemonize mode (fork and exit 0 after setup ok)")
	maxProcs := flag.Int("maxprocs", 2, "GOMAXPROCS limit (default: 2)")
	maxThreads := flag.Int("maxthreads", 1000, "Max OS threads limit (default: 1000)")
	gcPercent := flag.Int("gcpercent", 50, "GC percent target (default: 50)")
	reconcileInterval := flag.Duration("reconcile-interval", 1*time.Hour, "Interval for periodic session reconciliation (default: 1h). Set to 0 to disable.")
	flag.DurationVar(reconcileInterval, "reconcile", 1*time.Hour, "Alias for -reconcile-interval")

	flag.Parse()

	if *debugMode {
		*daemonize = false
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
	fmt.Printf("[aloha.svc] Performance settings: GOMAXPROCS=%d, MaxThreads=%d, GCPercent=%d, ReconcileInterval=%v\n", runtime.GOMAXPROCS(0), *maxThreads, *gcPercent, *reconcileInterval)

	if *debugMode {
		log.SetOutput(os.Stdout)
		log.Printf("[aloha.svc] Debug mode enabled (-D/-debug): logging directly to stdout")
	} else {
		// Configure logger to write to log file for all subsequent operational logs
		if err := os.MkdirAll(filepath.Dir(*logPath), 0755); err == nil {
			logFile, err := os.OpenFile(*logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
			if err == nil {
				log.SetOutput(logFile)
				// Write startup banner to log file as well
				log.Printf("[aloha.svc] Starting PTT BBS Aloha Service...")
				log.Printf("[aloha.svc] BBSHOME: %s, Log: %s", bbsHome, *logPath)
				log.Printf("[aloha.svc] Performance settings: GOMAXPROCS=%d, MaxThreads=%d, GCPercent=%d, ReconcileInterval=%v", runtime.GOMAXPROCS(0), *maxThreads, *gcPercent, *reconcileInterval)
			} else {
				log.Printf("[aloha.svc] Warning: failed to open log file %s: %v", *logPath, err)
			}
		}
	}

	service, err := daemon.NewService(bbsHome, socketPath)
	if err != nil {
		log.Fatalf("[aloha.svc] Failed to initialize Aloha Service: %v", err)
	}

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
