// mail.svc delivers mails from the Internet to BBS users (for
// util/bbsmail), sends mails to the Internet and packs homes (for mbbsd).
// See the daemon package for the protocol.
package main

import (
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"pttbbs/bbs"
	"pttbbs/mail/daemon"
)

// bbsLib is daemon.BBS on libcmbbs.
type bbsLib struct{}

func (bbsLib) LoadMailUser(userid string) (int, string, bool) {
	return bbs.LoadMailUser(userid)
}

func (bbsLib) DeliverHomeMail(userid string, owner, title, content []byte) error {
	return bbs.DeliverHomeMail(userid, owner, title, content)
}

func forkDaemon() error {
	execPath, err := os.Executable()
	if err != nil {
		execPath = os.Args[0]
	}
	var args []string
	for _, arg := range os.Args[1:] {
		if arg == "-d" || strings.HasPrefix(arg, "-d=") || arg == "--d" || strings.HasPrefix(arg, "--d=") {
			continue
		}
		args = append(args, arg)
	}
	args = append(args, "-d=false")
	cmd := exec.Command(execPath, args...)
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}
	return cmd.Start()
}

// confArg finds -conf (or -c) in args.
func confArg(args []string, def string) string {
	for i, arg := range args {
		name, val, hasVal := strings.Cut(strings.TrimLeft(arg, "-"), "=")
		if !strings.HasPrefix(arg, "-") || (name != "conf" && name != "c") {
			continue
		}
		if hasVal {
			return val
		}
		if i+1 < len(args) {
			return args[i+1]
		}
	}
	return def
}

// withConfig puts the settings in the config file (other than "plain",
// which the daemon reads by itself) before args, so args override them.
func withConfig(args []string, def string) []string {
	path := confArg(args, def)
	conf, err := daemon.ParseConfig(path)
	if err != nil {
		if !os.IsNotExist(err) {
			fmt.Fprintf(os.Stderr, "[mail.svc] Warning: config %s: %v\n", path, err)
		}
		return args
	}
	var res []string
	for _, e := range conf {
		switch {
		case e.Key == "plain" || e.Key == "conf" || e.Key == "c":
		case e.Value == "":
			res = append(res, "-"+e.Key)
		default:
			res = append(res, "-"+e.Key+"="+e.Value)
		}
	}
	return append(res, args...)
}

func main() {
	bbsHome := bbs.BBSHome()
	runDir := filepath.Join(bbsHome, "run")

	inSocket := flag.String("in-socket", filepath.Join(runDir, "mail.svc.in.sock"), "UNIX socket for util/bbsmail")
	outSocket := flag.String("out-socket", filepath.Join(runDir, "mail.svc.out.sock"), "UNIX socket for mbbsd")
	spoolDir := flag.String("spool", filepath.Join(bbsHome, "out", "mail.svc"), "Spool of mails to send")
	relay := flag.String("smtp", bbs.RelayServer(), "SMTP relay, host[:port]")
	zipLimitMB := flag.Int64("zip-limit", 25, "Largest home backup to mail, in MB")
	zipWorkers := flag.Int("zip-workers", 1, "Homes packed at the same time")
	maxAge := flag.Duration("max-age", 24*time.Hour, "Give up a mail not sent after this long")
	confFile := flag.String("conf", filepath.Join(bbsHome, daemon.ConfigPath),
		"Config file: \"key = value\" for these flags, and \"plain = pattern\" for receivers of plain mails")
	flag.StringVar(confFile, "c", *confFile, "Alias for -conf")
	logPath := flag.String("log", filepath.Join(bbsHome, "log", "mail.svc.log"), "Path to log file")
	debugMode := flag.Bool("D", false, "Debug mode (log to stdout, do not daemonize)")
	flag.BoolVar(debugMode, "debug", false, "Alias for -D")
	daemonize := flag.Bool("d", true, "Daemonize mode (fork and exit 0 after setup ok)")
	verbose := flag.Bool("v", false, "Verbose")
	if err := flag.CommandLine.Parse(withConfig(os.Args[1:], *confFile)); err != nil {
		os.Exit(2)
	}

	if *debugMode {
		*daemonize = false
	}
	if _, _, err := net.SplitHostPort(*relay); err != nil {
		*relay = net.JoinHostPort(*relay, "25")
	}

	if *daemonize {
		if daemon.IsSocketOccupied(*inSocket) || daemon.IsSocketOccupied(*outSocket) {
			fmt.Fprintf(os.Stderr, "[mail.svc] Error: another instance is running\n")
			os.Exit(1)
		}
		if err := forkDaemon(); err != nil {
			fmt.Fprintf(os.Stderr, "[mail.svc] Failed to daemonize: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("[mail.svc] Starting PTT BBS Mail Service in background...\n")
		os.Exit(0)
	}

	if !*debugMode {
		if err := os.MkdirAll(filepath.Dir(*logPath), 0755); err == nil {
			if f, err := os.OpenFile(*logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644); err == nil {
				log.SetOutput(f)
			}
		}
	}
	log.Printf("[mail.svc] Starting PTT BBS Mail Service... BBSHOME: %s", bbsHome)

	// libcmbbs works with paths relative to BBSHOME
	if err := os.Chdir(bbsHome); err != nil {
		log.Fatalf("[mail.svc] chdir %s: %v", bbsHome, err)
	}
	if _, err := bbs.AttachSHM(); err != nil {
		log.Fatalf("[mail.svc] %v", err)
	}

	srv, err := daemon.NewServer(daemon.Config{
		BBSHome:     bbsHome,
		InSocket:    *inSocket,
		OutSocket:   *outSocket,
		SpoolDir:    *spoolDir,
		Relay:       *relay,
		Hostname:    bbs.MyHostname(),
		StorageBig5: bbs.StorageIsBig5(),
		MBBig5:      bbs.MBIsBig5(),
		ZipLimit:    *zipLimitMB << 20,
		ZipWorkers:  *zipWorkers,
		MaxAge:      *maxAge,
		ConfFile:    *confFile,
		Verbose:     *verbose,
	}, bbsLib{})
	if err != nil {
		log.Fatalf("[mail.svc] %v", err)
	}
	if err := srv.Start(); err != nil {
		log.Fatalf("[mail.svc] Stopped: %v", err)
	}
	log.Printf("[mail.svc] Mail Service terminated.")
}
