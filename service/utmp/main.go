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

	"pttbbs/bbs"
	"pttbbs/utmp/daemon"
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
		} else if key == "debug" {
			key = "D"
		}

		// Normalize duration numbers without unit
		if key == "interval" || key == "fix-interval" {
			if n, err := strconv.Atoi(val); err == nil {
				if n >= 100000 {
					val = fmt.Sprintf("%dus", n)
				} else {
					val = fmt.Sprintf("%ds", n)
				}
			}
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

func main() {
	bbsHome := os.Getenv("BBSHOME")
	if bbsHome == "" {
		bbsHome = bbs.BBSHome()
	}

	rawArgs := os.Args[1:]

	// Determine configuration file path: default to $BBSHOME/etc/utmp.svc.conf
	confPath := filepath.Join(bbsHome, "etc", "utmp.svc.conf")
	if _, err := os.Stat(confPath); err != nil {
		confPath = ""
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
			fmt.Fprintf(os.Stderr, "[utmp.svc] Warning: failed to read config %s: %v\n", confPath, err)
		}
	}

	var verbose verboseValue

	confFlag := flag.String("conf", confPath, "Path to configuration file")
	flag.StringVar(confFlag, "c", confPath, "Alias for -conf")
	logPath := flag.String("log", "", "Path to log file (default: $BBSHOME/log/utmp.svc.log)")
	debugMode := flag.Bool("D", false, "Enable debug mode (log directly to stdout)")
	flag.BoolVar(debugMode, "debug", false, "Enable debug mode (alias for -D)")
	daemonize := flag.Bool("d", true, "Daemonize mode (fork and exit 0 after setup ok)")
	maxProcs := flag.Int("maxprocs", 2, "GOMAXPROCS limit (default: 2)")
	maxThreads := flag.Int("maxthreads", 1000, "Max OS threads limit (default: 1000)")
	gcPercent := flag.Int("gcpercent", 50, "GC percent target (default: 50)")
	interval := flag.Duration("interval", 1*time.Second, "Update interval for sortutmp (default: 1s)")
	niceVal := flag.Int("nice", -10, "OS nice priority level for utmp daemon (default: -10)")
	fixInterval := flag.Duration("fix-interval", 0, "Periodic utmpfix interval (default: 0 / disabled)")
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

	socketPath := filepath.Join(bbsHome, "run", "utmp.svc.sock")

	if *daemonize {
		if daemon.IsSocketOccupied(socketPath) {
			fmt.Fprintf(os.Stderr, "[utmp.svc] Error: UNIX domain socket %s is already occupied by another instance\n", socketPath)
			os.Exit(1)
		}

		if err := forkDaemon(cliArgs); err != nil {
			fmt.Fprintf(os.Stderr, "[utmp.svc] Failed to daemonize: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("[utmp.svc] Starting PTT BBS UTMP Service in background...\n")
		os.Exit(0)
	}

	// Try to set OS nice level if requested
	if *niceVal != 0 {
		if err := syscall.Setpriority(syscall.PRIO_PROCESS, 0, *niceVal); err != nil {
			if *debugMode || verbose > 0 {
				log.Printf("[utmp.svc] Notice: failed to set nice %d: %v (requires root or CAP_SYS_NICE)", *niceVal, err)
			}
		} else {
			if *debugMode || verbose > 0 {
				log.Printf("[utmp.svc] OS priority set to nice %d successfully", *niceVal)
			}
		}
	}

	if *logPath == "" {
		*logPath = filepath.Join(bbsHome, "log", "utmp.svc.log")
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

	fmt.Printf("[utmp.svc] Starting PTT BBS UTMP Service...\n")
	fmt.Printf("[utmp.svc] BBSHOME: %s, Config: %s, Log: %s\n", bbsHome, loadedConf, *logPath)
	fmt.Printf("[utmp.svc] Settings: Interval=%v, Nice=%d, GOMAXPROCS=%d, FixInterval=%v\n",
		*interval, *niceVal, runtime.GOMAXPROCS(0), *fixInterval)

	if *debugMode {
		log.SetOutput(os.Stdout)
		log.Printf("[utmp.svc] Debug mode enabled: logging directly to stdout (Verbose=%d)", int(verbose))
	} else {
		if err := os.MkdirAll(filepath.Dir(*logPath), 0755); err == nil {
			logFile, err := os.OpenFile(*logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
			if err == nil {
				log.SetOutput(logFile)
				log.Printf("[utmp.svc] Starting PTT BBS UTMP Service...")
				log.Printf("[utmp.svc] BBSHOME: %s, Config: %s, Log: %s", bbsHome, loadedConf, *logPath)
				log.Printf("[utmp.svc] Settings: Interval=%v, Nice=%d, GOMAXPROCS=%d, FixInterval=%v",
					*interval, *niceVal, runtime.GOMAXPROCS(0), *fixInterval)
			}
		}
	}

	var opts []daemon.ServiceOption
	opts = append(opts, daemon.WithInterval(*interval))
	opts = append(opts, daemon.WithVerbose(int(verbose)))
	if *fixInterval > 0 {
		opts = append(opts, daemon.WithFixInterval(*fixInterval))
	}

	service, err := daemon.NewService(bbsHome, socketPath, opts...)
	if err != nil {
		log.Fatalf("[utmp.svc] Failed to initialize UTMP Service: %v", err)
	}

	if err := service.Start(); err != nil {
		log.Fatalf("[utmp.svc] Service stopped with error: %v", err)
	}
}
