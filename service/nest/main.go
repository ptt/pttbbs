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

	"golang.org/x/sys/unix"

	"pttbbs/bbs"
	"pttbbs/nest/config"
	"pttbbs/nest/daemon"
	"pttbbs/nest/importer"
	"pttbbs/nest/model"
	"pttbbs/nest/storage"
)

var (
	flagConfig         = flag.String("config", "", "Path to nest.svc.conf (default: $BBSHOME/etc/nest.svc.conf)")
	flagBBSHome        = flag.String("bbshome", "", "Path to BBSHOME (default detected or /home/bbs)")
	flagDataDir        = flag.String("datadir", "", "Path to store Pebble and SQLite databases (default $BBSHOME/db/nest.db)")
	flagCacheDir       = flag.String("cachedir", "", "Path to store materialized Cache files (default $BBSHOME/cache)")
	flagUnixSocket     = flag.String("socket", "", "Path for modern UNIX domain socket IPC (default $BBSHOME/run/nest.svc.sock)")
	flagLogPath        = flag.String("log", "", "Path to log file (default: $BBSHOME/log/nest.svc.log)")
	flagDebugMode      = flag.Bool("D", false, "Enable debug mode (log directly to stdout, foreground)")
	flagDaemonize      = flag.Bool("d", true, "Daemonize mode (fork and exit 0 after setup ok)")
	flagCacheSizeMB    = flag.Int("cache-mb", 256, "Total Pebble Block Cache limit in MB")
	flagFlushSeconds   = flag.Int("flush-seconds", 2, "Interval in seconds to batch flush scores/counts to SQLite")
	flagMinReplyRunes  = flag.Int("min-reply-runes", 20, "Minimum net new characters for a reply post before auto-demoting to comment (0 to disable)")
	flagFilterEncoding = flag.String("filter-encoding", "big5", "Encoding for legacy import/export and materialized cache files (big5 or utf-8)")
	flagMaxOpenFiles   = flag.Int("max-files", 0, "Max open files per Pebble DB (0 for auto)")
	flagWorkers        = flag.Int("workers", 0, "Default worker threads for board import (0 to use config or default 8)")
	flagRebuild        = flag.Bool("rebuild", false, "Disaster recovery: rebuild SQLite from Pebble and exit")
	flagRecount        = flag.Bool("recount", false, "Count the comments and votes of every post again, correct the counters and exit (slow; after -rebuild if both)")
	flagVerify         = flag.Bool("verify", false, "Verify backup dirs given as arguments (read-only) and exit; non-zero exit on errors")
	flagQuick          = flag.Bool("quick", false, "With -verify: check DB integrity and the newest posts only (fast after a crash)")
)

func init() {
	flag.BoolVar(flagDebugMode, "debug", false, "Enable debug mode (alias for -D)")
	flag.StringVar(flagConfig, "c", "", "Path to nest.svc.conf (alias for -config)")
}

func isSocketOccupied(socketPath string) bool {
	conn, err := net.DialTimeout("unix", socketPath, 100*time.Millisecond)
	if err == nil {
		conn.Close()
		return true
	}
	return false
}

func forkDaemon(logPath string) error {
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
	if logPath != "" {
		_ = os.MkdirAll(filepath.Dir(logPath), 0755)
		if lf, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644); err == nil {
			cmd.Stdout = lf
			cmd.Stderr = lf
		}
	}

	return cmd.Start()
}

func raiseFDLimit() {
	var rlim unix.Rlimit
	if err := unix.Getrlimit(unix.RLIMIT_NOFILE, &rlim); err != nil {
		log.Printf("[nest.svc] warning: failed to query RLIMIT_NOFILE: %v", err)
		return
	}
	target := uint64(65536)
	if rlim.Max > 0 && rlim.Max < target {
		target = rlim.Max
	}
	if rlim.Cur < target {
		oldCur := rlim.Cur
		rlim.Cur = target
		if err := unix.Setrlimit(unix.RLIMIT_NOFILE, &rlim); err != nil {
			rlim.Cur = rlim.Max
			_ = unix.Setrlimit(unix.RLIMIT_NOFILE, &rlim)
		}
		_ = unix.Getrlimit(unix.RLIMIT_NOFILE, &rlim)
		log.Printf("[nest.svc] Adjusted RLIMIT_NOFILE from %d to %d (max: %d)", oldCur, rlim.Cur, rlim.Max)
	}
	if rlim.Cur < 4096 {
		log.Printf("[nest.svc] WARNING: RLIMIT_NOFILE is low (%d). Pebble may exhaust file descriptors during heavy loads. Run 'ulimit -n 65536' before launching nest.svc.", rlim.Cur)
	}
}

func main() {
	flag.Parse()
	if *flagVerify {
		os.Exit(runVerify(flag.Args(), *flagQuick))
	}
	raiseFDLimit()

	bbshome := *flagBBSHome
	if bbshome == "" {
		bbshome = bbs.BBSHome()
		if bbshome == "" {
			bbshome = "/home/bbs"
		}
	}

	confPath := config.FindConfigFile(*flagConfig, bbshome)
	var svcConf *config.ServiceConfig
	if confPath != "" {
		loaded, err := config.LoadConfigFile(confPath, bbshome)
		if err != nil {
			log.Printf("[nest.svc] Warning: failed to load config %s: %v", confPath, err)
			svcConf = config.DefaultConfig(bbshome)
		} else {
			svcConf = loaded
			log.Printf("[nest.svc] Loaded configuration from %s (%d engines, %d pinned boards)",
				confPath, len(svcConf.Engines), len(svcConf.Pins))
		}
	} else {
		svcConf = config.DefaultConfig(bbshome)
	}

	// CLI flags override config file values if explicitly specified
	if *flagDataDir != "" && len(svcConf.Engines) <= 1 {
		svcConf.Engines[0].DataDir = *flagDataDir
	}
	if *flagCacheDir != "" && len(svcConf.Engines) <= 1 {
		svcConf.Engines[0].CacheDir = *flagCacheDir
	}
	if *flagUnixSocket != "" {
		svcConf.UnixSocket = *flagUnixSocket
	}
	if *flagLogPath != "" {
		svcConf.LogPath = *flagLogPath
	}
	if *flagCacheSizeMB != 256 {
		svcConf.CacheSizeMB = *flagCacheSizeMB
	}
	if *flagFlushSeconds != 2 {
		svcConf.FlushSeconds = *flagFlushSeconds
	}
	if *flagMinReplyRunes != 20 {
		svcConf.MinReplyRunes = *flagMinReplyRunes
	}
	if *flagFilterEncoding != "big5" {
		svcConf.FilterEncoding = *flagFilterEncoding
	}
	if *flagMaxOpenFiles > 0 {
		svcConf.MaxOpenFiles = *flagMaxOpenFiles
	}
	if *flagWorkers > 0 {
		svcConf.ImportWorkers = *flagWorkers
	}

	sockPath := svcConf.UnixSocket
	logPath := svcConf.LogPath

	if *flagDebugMode || *flagRebuild || *flagRecount {
		*flagDaemonize = false
	}

	if *flagDaemonize {
		if isSocketOccupied(sockPath) {
			fmt.Fprintf(os.Stderr, "[nest.svc] Error: UNIX domain socket %s is already occupied by another instance\n", sockPath)
			os.Exit(1)
		}

		if err := forkDaemon(logPath); err != nil {
			fmt.Fprintf(os.Stderr, "[nest.svc] Failed to daemonize: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("[nest.svc] Starting PTT BBS Post Service in background...\n")
		os.Exit(0)
	}

	if *flagDebugMode {
		log.SetOutput(os.Stdout)
		log.Printf("[nest.svc] Debug mode enabled (-D/-debug): logging directly to stdout")
	} else {
		if err := os.MkdirAll(filepath.Dir(logPath), 0755); err == nil {
			if logFile, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644); err == nil {
				log.SetOutput(logFile)
				_ = unix.Dup2(int(logFile.Fd()), 1)
				_ = unix.Dup2(int(logFile.Fd()), 2)
			} else {
				log.Printf("[nest.svc] Warning: failed to open log file %s: %v", logPath, err)
			}
		}
	}

	log.Printf("[nest.svc] Initializing Reddit-aligned post & comment service...")
	log.Printf("[nest.svc] BBSHOME: %s, Engines: %d, Socket: %s, Log: %s, FilterEncoding: %s",
		svcConf.BBSHome, len(svcConf.Engines), sockPath, logPath, svcConf.FilterEncoding)
	for i, eng := range svcConf.Engines {
		log.Printf("[nest.svc]   Engine[%d]: DataDir=%s, CacheDir=%s", i, eng.DataDir, eng.CacheDir)
	}
	if len(svcConf.Engines) > 0 {
		log.Printf("[nest.svc]   Communities: %s", filepath.Join(svcConf.Engines[0].DataDir, storage.CommunitiesDBName))
	}

	st, err := storage.OpenStorage(svcConf)
	if err != nil {
		log.Fatalf("[nest.svc] Failed to open storage: %v", err)
	}
	defer st.Close()

	if *flagRebuild {
		log.Printf("[nest.svc] Starting disaster recovery rebuild of SQLite from Pebble...")
		topics, posts, err := st.RebuildSQLiteFromPebble()
		if err != nil {
			log.Fatalf("[nest.svc] Rebuild failed: %v", err)
		}
		log.Printf("[nest.svc] Successfully rebuilt SQLite metadata! Topics: %d, Posts: %d", topics, posts)
	}
	if *flagRecount {
		log.Printf("[nest.svc] Recounting the comments and votes of every post...")
		fixed, err := st.RecountPosts()
		if err != nil {
			log.Fatalf("[nest.svc] Recount failed: %v", err)
		}
		log.Printf("[nest.svc] Recount corrected %d posts", fixed)
	}
	if *flagRebuild || *flagRecount {
		return
	}

	// Resolves the person IDs of the users in legacy articles; without SHM they are
	// imported as unknown.
	var users importer.UserResolver
	if shm, err := bbs.AttachSHM(); err == nil {
		resolver := bbs.NewUserResolver(shm)
		users = resolver
		// Persons shown by the Lemmy API get their BBS userid and nickname.
		st.Communities().SetPersonLookup(func(user model.PersonID) (string, string, bool) {
			return resolver.LookupAccount(user.Usernum(), user.Firstlogin())
		})
	} else {
		log.Printf("[nest.svc] Warning: %v; person IDs of legacy authors will not be resolved", err)
	}

	srv := daemon.NewServer(daemon.ServerConfig{
		BBSHome:        svcConf.BBSHome,
		UnixSocket:     sockPath,
		FilterEncoding: svcConf.FilterEncoding,
		ImportWorkers:  svcConf.ImportWorkers,
		SiteURL:        svcConf.SiteURL,
		Users:          users,
		// The communities follow .BRD: what changed while nest.svc was down.
		SyncBoardsAtStart: true,
		Crosspost: storage.CrosspostLimits{
			Window:       int64(svcConf.CrosspostWindowSec),
			PersonMax:    svcConf.CrosspostPersonMax,
			PostMax:      svcConf.CrosspostPostMax,
			ExemptBoards: svcConf.CrosspostExemptBoards,
		},
	}, st)

	if err := srv.Start(); err != nil {
		log.Fatalf("[nest.svc] Failed to start server: %v", err)
	}

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	sig := <-sigCh

	log.Printf("[nest.svc] Received signal %v, shutting down gracefully...", sig)
	srv.Stop()
	log.Printf("[nest.svc] Service stopped cleanly.")
}

// runVerify implements `nest.svc -verify [-quick] <dir>...`. Each dir is an
// engine data dir (posts.pebble, ..., meta.sqlite3) or a dir with shard-<N> subdirs.
func runVerify(args []string, quick bool) int {
	if len(args) == 0 {
		fmt.Fprintln(os.Stderr, "Usage: nest.svc -verify [-quick] <backup_dir>...")
		return 2
	}
	dirs, err := storage.ExpandVerifyDirs(args)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		return 2
	}
	communities := storage.FindCommunitiesDB(dirs)
	rc := 0
	for _, dir := range dirs {
		var r *storage.VerifyReport
		if quick {
			r = storage.VerifyDataDirQuick(dir, communities, storage.DefaultQuickVerifyTail)
		} else {
			r = storage.VerifyDataDir(dir, communities)
		}
		r.Print(os.Stdout)
		if !r.OK() {
			rc = 1
		}
	}
	if !quick && len(dirs) > 1 && rc == 0 {
		r := storage.VerifyShards(dirs, communities)
		r.Print(os.Stdout)
		if !r.OK() {
			rc = 1
		}
	}
	return rc
}
