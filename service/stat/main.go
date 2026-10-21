package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"pttbbs/bbs"
	"pttbbs/stat/daemon"
	"pttbbs/stat/storage"
)

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

func main() {
	bbsHome := bbs.BBSHome()

	defaultSocket := filepath.Join(bbsHome, "run", "stat.svc.sock")
	defaultDB := filepath.Join(bbsHome, "run", "stat.db")
	defaultLog := filepath.Join(bbsHome, "log", "stat.svc.log")

	defaultInterval := 1 * time.Hour
	socketPath := flag.String("socket", defaultSocket, "Path to UNIX domain socket")
	dbPath := flag.String("db", defaultDB, "Path to JSON database file for persistence")
	logPath := flag.String("log", defaultLog, "Path to log file")
	saveInterval := flag.Duration("save-interval", defaultInterval, "Interval between batch saves/flush to disk (default: 1h)")
	flag.DurationVar(saveInterval, "flush-interval", defaultInterval, "Alias for -save-interval")
	debugMode := flag.Bool("D", false, "Enable debug mode (log directly to stdout, do not daemonize)")
	flag.BoolVar(debugMode, "debug", false, "Enable debug mode (alias for -D)")
	daemonize := flag.Bool("d", true, "Daemonize mode (fork and exit 0 after setup ok)")

	flag.Parse()

	if *debugMode {
		*daemonize = false
	}

	if *daemonize {
		if daemon.IsSocketOccupied(*socketPath) {
			fmt.Fprintf(os.Stderr, "[stat.svc] Error: UNIX domain socket %s is already occupied by another instance\n", *socketPath)
			os.Exit(1)
		}

		if err := forkDaemon(); err != nil {
			fmt.Fprintf(os.Stderr, "[stat.svc] Failed to daemonize: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("[stat.svc] Starting PTT BBS Stat Service in background...\n")
		os.Exit(0)
	}

	if !*debugMode {
		if err := os.MkdirAll(filepath.Dir(*logPath), 0755); err == nil {
			if f, err := os.OpenFile(*logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644); err == nil {
				log.SetOutput(f)
			}
		}
	} else {
		log.Printf("[stat.svc] Debug mode enabled (-D/-debug): logging directly to stdout")
	}

	log.Printf("[stat.svc] Starting PTT BBS Stat Service...")
	log.Printf("[stat.svc] BBSHOME: %s, Socket: %s, DB: %s", bbsHome, *socketPath, *dbPath)

	store := storage.NewStorage(*dbPath)
	if n, err := store.Load(); err != nil {
		log.Printf("[stat.svc] Warning: failed to load existing stats: %v", err)
	} else if n > 0 {
		log.Printf("[stat.svc] Loaded %d active user daily stats from disk", n)
	}

	server := daemon.NewServer(*socketPath, store, *saveInterval)
	if err := server.Start(); err != nil {
		log.Fatalf("[stat.svc] Server stopped with error: %v", err)
	}
	log.Printf("[stat.svc] Stat Service terminated.")
}
