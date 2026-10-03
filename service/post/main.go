package main

import (
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"pttbbs/bbs"
	"pttbbs/post/daemon"
	"pttbbs/post/storage"
)

var (
	flagBBSHome        = flag.String("bbshome", "", "Path to BBSHOME (default detected or /home/bbs)")
	flagDataDir        = flag.String("datadir", "", "Path to store Pebble and SQLite databases (default $BBSHOME/db)")
	flagCacheDir       = flag.String("cachedir", "", "Path to store materialized Cache files (default $BBSHOME/cache)")
	flagUnixSocket     = flag.String("socket", "", "Path for modern UNIX domain socket IPC (default $BBSHOME/run/post.svc.sock)")
	flagLogPath        = flag.String("log", "", "Path to log file (default: $BBSHOME/log/post.svc.log)")
	flagDebugMode      = flag.Bool("D", false, "Enable debug mode (log directly to stdout, foreground)")
	flagDaemonize      = flag.Bool("d", true, "Daemonize mode (fork and exit 0 after setup ok)")
	flagCacheSizeMB    = flag.Int("cache-mb", 256, "Total Pebble Block Cache limit in MB")
	flagFlushSeconds   = flag.Int("flush-seconds", 2, "Interval in seconds to batch flush scores/counts to SQLite")
	flagMinReplyRunes  = flag.Int("min-reply-runes", 20, "Minimum net new characters for a reply post before auto-demoting to comment (0 to disable)")
	flagFilterEncoding = flag.String("filter-encoding", "big5", "Encoding for legacy import/export and materialized cache files (big5 or utf-8)")
	flagRebuild        = flag.Bool("rebuild", false, "Disaster recovery: rebuild SQLite from Pebble and exit")
	flagVerbose        = flag.Bool("v", false, "Enable verbose debug logging")
)

func init() {
	flag.BoolVar(flagDebugMode, "debug", false, "Enable debug mode (alias for -D)")
}

func isSocketOccupied(socketPath string) bool {
	conn, err := net.DialTimeout("unix", socketPath, 100*time.Millisecond)
	if err == nil {
		conn.Close()
		return true
	}
	return false
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

func main() {
	flag.Parse()

	bbshome := *flagBBSHome
	if bbshome == "" {
		bbshome = bbs.BBSHome()
		if bbshome == "" {
			bbshome = "/home/bbs"
		}
	}

	dataDir := *flagDataDir
	if dataDir == "" {
		dataDir = filepath.Join(bbshome, "db")
	}

	cacheDir := *flagCacheDir
	if cacheDir == "" {
		cacheDir = filepath.Join(bbshome, "cache")
	}

	sockPath := *flagUnixSocket
	if sockPath == "" {
		sockPath = filepath.Join(bbshome, "run", "post.svc.sock")
	}

	if *flagDebugMode || *flagRebuild {
		*flagDaemonize = false
	}

	if *flagDaemonize {
		if isSocketOccupied(sockPath) {
			fmt.Fprintf(os.Stderr, "[post.svc] Error: UNIX domain socket %s is already occupied by another instance\n", sockPath)
			os.Exit(1)
		}

		if err := forkDaemon(); err != nil {
			fmt.Fprintf(os.Stderr, "[post.svc] Failed to daemonize: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("[post.svc] Starting PTT BBS Post Service in background...\n")
		os.Exit(0)
	}

	if *flagLogPath == "" {
		*flagLogPath = filepath.Join(bbshome, "log", "post.svc.log")
	}

	if *flagDebugMode {
		log.SetOutput(os.Stdout)
		log.Printf("[post.svc] Debug mode enabled (-D/-debug): logging directly to stdout")
	} else {
		if err := os.MkdirAll(filepath.Dir(*flagLogPath), 0755); err == nil {
			if logFile, err := os.OpenFile(*flagLogPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644); err == nil {
				log.SetOutput(logFile)
			} else {
				log.Printf("[post.svc] Warning: failed to open log file %s: %v", *flagLogPath, err)
			}
		}
	}

	log.Printf("[post.svc] Initializing Reddit-aligned post & comment service...")
	log.Printf("[post.svc] BBSHOME: %s, DataDir: %s, CacheDir: %s, Socket: %s, Log: %s, FilterEncoding: %s",
		bbshome, dataDir, cacheDir, sockPath, *flagLogPath, *flagFilterEncoding)

	st, err := storage.OpenEngine(storage.Config{
		BBSHome:        bbshome,
		DataDir:        dataDir,
		CacheDir:       cacheDir,
		CacheSizeMB:    *flagCacheSizeMB,
		FlushSeconds:   *flagFlushSeconds,
		MinReplyRunes:  *flagMinReplyRunes,
		FilterEncoding: *flagFilterEncoding,
	})
	if err != nil {
		log.Fatalf("[post.svc] Failed to open storage engine: %v", err)
	}
	defer st.Close()

	if *flagRebuild {
		log.Printf("[post.svc] Starting disaster recovery rebuild of SQLite from Pebble...")
		topics, posts, err := st.RebuildSQLiteFromPebble()
		if err != nil {
			log.Fatalf("[post.svc] Rebuild failed: %v", err)
		}
		log.Printf("[post.svc] Successfully rebuilt SQLite metadata! Topics: %d, Posts: %d", topics, posts)
		return
	}

	srv := daemon.NewServer(daemon.ServerConfig{
		BBSHome:        bbshome,
		UnixSocket:     sockPath,
		FilterEncoding: *flagFilterEncoding,
	}, st)

	if err := srv.Start(); err != nil {
		log.Fatalf("[post.svc] Failed to start server: %v", err)
	}

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	sig := <-sigCh

	log.Printf("[post.svc] Received signal %v, shutting down gracefully...", sig)
	srv.Stop()
	log.Printf("[post.svc] Service stopped cleanly.")
}
