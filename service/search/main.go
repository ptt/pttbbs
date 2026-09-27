package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	_ "net/http/pprof"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"runtime/debug"
	"strconv"
	"strings"
	"syscall"

	"pttbbs/bbs"
	"pttbbs/search/daemon"
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

func parseConfigFile(path string) ([]string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var args []string
	lines := strings.Split(string(data), "\n")
	for _, rawLine := range lines {
		line := strings.TrimSpace(rawLine)
		if line == "" || strings.HasPrefix(line, "#") || strings.HasPrefix(line, ";") || strings.HasPrefix(line, "//") {
			continue
		}

		if strings.HasPrefix(line, "-") {
			if eqIdx := strings.Index(line, "="); eqIdx != -1 {
				k := strings.TrimSpace(line[:eqIdx])
				v := strings.Trim(strings.TrimSpace(line[eqIdx+1:]), `"'`)
				args = append(args, fmt.Sprintf("%s=%s", k, v))
				continue
			}
			parts := strings.Fields(line)
			if len(parts) == 1 {
				args = append(args, parts[0])
			} else {
				args = append(args, parts[0], strings.Trim(parts[1], `"'`))
			}
			continue
		}

		var key, val string
		if eqIdx := strings.IndexAny(line, "=:"); eqIdx != -1 {
			key = strings.TrimSpace(line[:eqIdx])
			val = strings.TrimSpace(line[eqIdx+1:])
		} else {
			parts := strings.Fields(line)
			key = parts[0]
			if len(parts) > 1 {
				val = strings.Join(parts[1:], " ")
			}
		}

		key = strings.ToLower(key)
		key = strings.ReplaceAll(key, "_", "-")
		val = strings.Trim(val, `"'`)

		if key == "daemon" || key == "daemonize" {
			key = "d"
		}

		flagKey := "-" + key
		if val == "" {
			args = append(args, flagKey)
		} else {
			args = append(args, fmt.Sprintf("%s=%s", flagKey, val))
		}
	}
	return args, nil
}

func main() {
	bbsHome := os.Getenv("BBSHOME")
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}

	rawArgs := os.Args[1:]

	// Determine configuration file path: default to $BBSHOME/etc/search.conf
	confPath := filepath.Join(bbsHome, "etc", "search.conf")
	if _, err := os.Stat(confPath); err != nil {
		alt := filepath.Join(bbsHome, "etc", "search.svc.conf")
		if _, err2 := os.Stat(alt); err2 == nil {
			confPath = alt
		} else {
			confPath = ""
		}
	}

	// Allow overriding config path from command line
	for i := 0; i < len(rawArgs); i++ {
		arg := rawArgs[i]
		if (arg == "-conf" || arg == "--conf" || arg == "-c") && i+1 < len(rawArgs) {
			confPath = rawArgs[i+1]
			break
		}
		if strings.HasPrefix(arg, "-conf=") || strings.HasPrefix(arg, "--conf=") || strings.HasPrefix(arg, "-c=") {
			confPath = strings.SplitN(arg, "=", 2)[1]
			break
		}
	}

	var loadedConf string
	var configArgs []string
	if confPath != "" && confPath != "none" {
		if args, err := parseConfigFile(confPath); err == nil {
			configArgs = args
			loadedConf = confPath
		} else if !os.IsNotExist(err) {
			fmt.Fprintf(os.Stderr, "[search.svc] Warning: failed to read config %s: %v\n", confPath, err)
		}
	}

	var verbose verboseValue

	confFlag := flag.String("conf", confPath, "Path to configuration file")
	flag.StringVar(confFlag, "c", confPath, "Alias for -conf")
	logPath := flag.String("log", "", "Path to log file (default: $BBSHOME/log/search.svc.log)")
	debugMode := flag.Bool("D", false, "Enable debug mode (log directly to stdout)")
	flag.BoolVar(debugMode, "debug", false, "Enable debug mode (alias for -D)")
	daemonize := flag.Bool("d", true, "Daemonize mode (fork and exit 0 after setup ok)")
	maxProcs := flag.Int("maxprocs", 4, "GOMAXPROCS limit (default: 4)")
	maxThreads := flag.Int("maxthreads", 1000, "Max OS threads limit (default: 1000)")
	gcPercent := flag.Int("gcpercent", 50, "GC percent target (default: 50)")
	maxEntries := flag.Int("max-entries", daemon.DefaultMaxEntries, "Max cached search entries (default: 2048)")
	maxIndices := flag.Int64("max-indices", daemon.DefaultMaxIndices, "Max total cached int32 indices (default: 16M ~ 64MB)")
	maxAIDEntries := flag.Int("max-aid-entries", daemon.DefaultMaxAIDEntries, "Max cached AID lookup entries (default: 16384)")
	maxAIDTables := flag.Int("max-aid-tables", daemon.DefaultMaxAIDTables, "Max boards with cached in-memory AID table (default: 64, 0=disabled)")
	aidTableMinReqs := flag.Int64("aid-table-min-reqs", daemon.DefaultAIDTableMinReqs, "Min cumulative queries (search + aid) to admit board into cache (default: 100)")
	flag.Int64Var(aidTableMinReqs, "min-cache-queries", daemon.DefaultAIDTableMinReqs, "Alias for -aid-table-min-reqs")
	aidTableTTL := flag.Duration("aid-table-ttl", daemon.DefaultAIDTableTTL, "Min duration before cached board can be replaced (default: 1h)")
	aidTableEvictLead := flag.Int64("aid-table-evict-lead", daemon.DefaultAIDTableEvictLead, "Query lead required to replace an expired cached board (default: 100)")
	cacheTTL := flag.Duration("cache-ttl", daemon.DefaultCacheTTL, "Cache entry TTL (default: 1h)")
	pprofAddr := flag.String("pprof-addr", "", "Listen address for HTTP pprof server (e.g. 127.0.0.1:6060)")
	flag.Var(&verbose, "v", "Verbose mode (can be specified multiple times, e.g. -v -v or -vv)")
	flag.Var(&verbose, "verbose", "Alias for -v")

	cliArgs := preprocessArgs(rawArgs)
	allArgs := append(configArgs, cliArgs...)
	os.Args = append([]string{os.Args[0]}, allArgs...)
	flag.Parse()

	if *debugMode {
		*daemonize = false
		verbose += 10
	}

	socketPath := filepath.Join(bbsHome, "run", "search.svc.sock")

	if *daemonize {
		if daemon.IsSocketOccupied(socketPath) {
			fmt.Fprintf(os.Stderr, "[search.svc] Error: UNIX domain socket %s is already occupied by another instance\n", socketPath)
			os.Exit(1)
		}

		// Check SHM before forking so that the caller sees the failure.
		if _, err := bbs.AttachSHM(); err != nil {
			fmt.Fprintf(os.Stderr, "[search.svc] Error: %v (run shmctl init first)\n", err)
			os.Exit(1)
		}

		if err := forkDaemon(cliArgs); err != nil {
			fmt.Fprintf(os.Stderr, "[search.svc] Failed to daemonize: %v\n", err)
			os.Exit(1)
		}
		if loadedConf != "" {
			fmt.Printf("[search.svc] Starting PTT BBS Search Cache Service in background (config: %s)...\n", loadedConf)
		} else {
			fmt.Printf("[search.svc] Starting PTT BBS Search Cache Service in background...\n")
		}
		os.Exit(0)
	}

	if *logPath == "" {
		*logPath = filepath.Join(bbsHome, "log", "search.svc.log")
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

	fmt.Printf("[search.svc] Starting PTT BBS Search Cache Service...\n")
	fmt.Printf("[search.svc] BBSHOME: %s, Log: %s\n", bbsHome, *logPath)
	if loadedConf != "" {
		fmt.Printf("[search.svc] Config: %s\n", loadedConf)
	}
	fmt.Printf("[search.svc] Performance settings: GOMAXPROCS=%d, MaxThreads=%d, GCPercent=%d, Verbose=%d\n",
		runtime.GOMAXPROCS(0), *maxThreads, *gcPercent, int(verbose))
	fmt.Printf("[search.svc] Cache limits: max-entries=%d, max-indices=%d, max-aid-entries=%d, max-aid-tables=%d, cache-ttl=%v\n",
		*maxEntries, *maxIndices, *maxAIDEntries, *maxAIDTables, *cacheTTL)

	if *debugMode {
		log.SetOutput(os.Stdout)
	} else {
		if err := os.MkdirAll(filepath.Dir(*logPath), 0755); err == nil {
			logFile, err := os.OpenFile(*logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
			if err == nil {
				log.SetOutput(logFile)
				log.Printf("[search.svc] Starting PTT BBS Search Cache Service...")
				log.Printf("[search.svc] BBSHOME: %s, Log: %s", bbsHome, *logPath)
				if loadedConf != "" {
					log.Printf("[search.svc] Config: %s", loadedConf)
				}
				log.Printf("[search.svc] Performance settings: GOMAXPROCS=%d, MaxThreads=%d, GCPercent=%d, Verbose=%d",
					runtime.GOMAXPROCS(0), *maxThreads, *gcPercent, int(verbose))
				log.Printf("[search.svc] Cache limits: max-entries=%d, max-indices=%d, max-aid-entries=%d, max-aid-tables=%d, cache-ttl=%v",
					*maxEntries, *maxIndices, *maxAIDEntries, *maxAIDTables, *cacheTTL)
			}
		}
	}

	service, err := daemon.NewService(bbsHome, socketPath)
	if err != nil {
		log.Fatalf("[search.svc] Failed to initialize Search Service: %v", err)
	}
	service.SetVerbose(int(verbose))
	service.SetCacheLimits(*maxEntries, *maxIndices, *maxAIDEntries, *maxAIDTables, *cacheTTL)
	service.SetAIDTablePolicy(*aidTableMinReqs, *aidTableTTL, *aidTableEvictLead)

	if *pprofAddr != "" {
		go func() {
			log.Printf("[search.svc] Starting HTTP pprof server on %s", *pprofAddr)
			if err := http.ListenAndServe(*pprofAddr, nil); err != nil {
				log.Printf("[search.svc] HTTP pprof server stopped: %v", err)
			}
		}()
	}

	if err := service.Start(); err != nil {
		log.Fatalf("[search.svc] Service stopped with error: %v", err)
	}
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
