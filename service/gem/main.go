// gem.svc keeps the gem areas (精華區, man/) of the boards in a Pebble
// database, as copy-on-write trees (see package storage), and serves them on
// a UNIX socket (see package protocol).
package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	_ "net/http/pprof"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime/debug"
	"strings"
	"syscall"

	"golang.org/x/sys/unix"

	"pttbbs/bbs"
	"pttbbs/gem/config"
	"pttbbs/gem/daemon"
	"pttbbs/gem/storage"
)

var (
	flagConfig  = flag.String("config", "", "Path to gem.svc.conf (default: $BBSHOME/etc/gem.svc.conf)")
	flagBBSHome = flag.String("bbshome", "", "Path to BBSHOME (default: as built)")
	flagDataDir = flag.String("datadir", "", "Where the database is kept (default: $BBSHOME/db/gem.db)")
	flagSocket  = flag.String("socket", "", "UNIX socket (default: $BBSHOME/run/gem.svc.sock)")
	flagLog     = flag.String("log", "", "Log file (default: $BBSHOME/log/gem.svc.log)")
	flagDebug   = flag.Bool("D", false, "Debug: stay in the foreground and log to stdout")
	flagDaemon  = flag.Bool("d", true, "Fork into the background once started")
	flagCacheMB = flag.Int("cache-mb", 0, "Pebble block cache in MB (default: config or 128)")
	flagWorkers = flag.Int("workers", 0, "Files read in parallel by an import (default: config or 8)")
	flagPprof   = flag.String("pprof", "", "Serve Go's pprof on this address (e.g. localhost:6060), to profile")
	flagGC      = flag.Int("gcpercent", 400, "GOGC: garbage collected when the heap grows by this percent (imports read much)")
)

func init() {
	flag.BoolVar(flagDebug, "debug", false, "Alias for -D")
	flag.StringVar(flagConfig, "c", "", "Alias for -config")
}

func forkDaemon(logPath string) error {
	exe, err := os.Executable()
	if err != nil {
		exe = os.Args[0]
	}
	var args []string
	for _, a := range os.Args[1:] {
		if a == "-d" || strings.HasPrefix(a, "-d=") || a == "--d" || strings.HasPrefix(a, "--d=") {
			continue
		}
		args = append(args, a)
	}
	args = append(args, "-d=false")
	cmd := exec.Command(exe, args...)
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}
	if logPath != "" {
		_ = os.MkdirAll(filepath.Dir(logPath), 0755)
		if lf, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644); err == nil {
			cmd.Stdout = lf
			cmd.Stderr = lf
		}
	}
	return cmd.Start()
}

// raiseFDLimit raises RLIMIT_NOFILE (up to 65536): the two Pebble
// databases keep a quarter of it open each.
func raiseFDLimit() {
	var rlim unix.Rlimit
	if err := unix.Getrlimit(unix.RLIMIT_NOFILE, &rlim); err != nil {
		return
	}
	target := uint64(65536)
	if rlim.Max < target {
		target = rlim.Max
	}
	if rlim.Cur < target {
		rlim.Cur = target
		_ = unix.Setrlimit(unix.RLIMIT_NOFILE, &rlim)
	}
}

func main() {
	flag.Parse()
	raiseFDLimit()
	bbsHome := *flagBBSHome
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}
	cfg, err := config.Load(config.Path(*flagConfig, bbsHome), bbsHome)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[gem.svc] %v\n", err)
		os.Exit(1)
	}
	if *flagDataDir != "" {
		cfg.DataDir = *flagDataDir
	}
	if *flagSocket != "" {
		cfg.UnixSocket = *flagSocket
	}
	if *flagLog != "" {
		cfg.LogPath = *flagLog
	}
	if *flagCacheMB > 0 {
		cfg.CacheMB = *flagCacheMB
	}
	if *flagWorkers > 0 {
		cfg.Workers = *flagWorkers
	}
	if *flagDebug {
		*flagDaemon = false
	}

	if *flagDaemon {
		if daemon.IsSocketOccupied(cfg.UnixSocket) {
			fmt.Fprintf(os.Stderr, "[gem.svc] %s is already served by another instance\n", cfg.UnixSocket)
			os.Exit(1)
		}
		if err := forkDaemon(cfg.LogPath); err != nil {
			fmt.Fprintf(os.Stderr, "[gem.svc] cannot fork: %v\n", err)
			os.Exit(1)
		}
		fmt.Println("[gem.svc] Starting PTT BBS Gem Service in background...")
		os.Exit(0)
	}

	if *flagDebug {
		log.SetOutput(os.Stdout)
	} else if cfg.LogPath != "" {
		_ = os.MkdirAll(filepath.Dir(cfg.LogPath), 0755)
		if lf, err := os.OpenFile(cfg.LogPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644); err == nil {
			log.SetOutput(lf)
		}
	}
	log.Printf("[gem.svc] BBSHOME %s, data %s, quota %v (%d boards with their own)", bbsHome, cfg.DataDir, cfg.Quota, len(cfg.Boards))

	if err := os.MkdirAll(cfg.DataDir, 0755); err != nil {
		log.Fatalf("[gem.svc] %v", err)
	}
	st, err := storage.Open(storage.Options{
		Dir:     config.PebbleDir(cfg.DataDir),
		CacheMB: cfg.CacheMB,
		Limits:  cfg.LimitsFor,
	})
	if err != nil {
		log.Fatalf("[gem.svc] cannot open %s: %v", cfg.DataDir, err)
	}
	if *flagGC > 0 {
		debug.SetGCPercent(*flagGC)
	}
	if *flagPprof != "" {
		go func() {
			log.Printf("[gem.svc] pprof on http://%s/debug/pprof/", *flagPprof)
			if err := http.ListenAndServe(*flagPprof, nil); err != nil {
				log.Printf("[gem.svc] pprof: %v", err)
			}
		}()
	}
	srv := daemon.New(daemon.Config{
		BBSHome:    bbsHome,
		UnixSocket: cfg.UnixSocket,
		DataDir:    cfg.DataDir,
		Workers:    cfg.Workers,
		Limits:     cfg.LimitsFor,
	}, st)
	if err := srv.Start(); err != nil {
		st.Close()
		log.Fatalf("[gem.svc] %v", err)
	}
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	s := <-sig
	log.Printf("[gem.svc] %v: stopping", s)
	srv.Stop()
	if err := st.Close(); err != nil {
		log.Printf("[gem.svc] closing the database: %v", err)
	}
	os.Remove(cfg.UnixSocket)
	log.Printf("[gem.svc] stopped")
}
