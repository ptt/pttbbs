package main

import (
	"flag"
	"io"
	"log"
	"os"
	"path/filepath"

	"pttbbs/aloha/daemon"
	"pttbbs/bbs"
)

func main() {
	bbsHome := bbs.BBSHome()

	logPath := flag.String("log", "", "Path to log file (default: $BBSHOME/run/aloha.svc.log)")
	debugMode := flag.Bool("d", false, "Enable debug mode (log directly to stdout)")
	flag.BoolVar(debugMode, "debug", false, "Enable debug mode (alias for -d)")

	flag.Parse()

	if *logPath == "" {
		*logPath = filepath.Join(bbsHome, "run", "aloha.svc.log")
	}

	log.SetFlags(log.LstdFlags)

	if *debugMode {
		log.SetOutput(os.Stdout)
		log.Printf("[aloha.svc] Debug mode enabled (-d): logging directly to stdout")
	} else {
		// Configure logger to write to log file if log file can be opened
		if err := os.MkdirAll(filepath.Dir(*logPath), 0755); err == nil {
			logFile, err := os.OpenFile(*logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
			if err == nil {
				defer logFile.Close()
				mw := io.MultiWriter(os.Stdout, logFile)
				log.SetOutput(mw)
			}
		}
	}

	log.Printf("[aloha.svc] Starting PTT BBS Aloha Service...")
	log.Printf("[aloha.svc] BBSHOME: %s, Log: %s", bbsHome, *logPath)

	service, err := daemon.NewService(bbsHome)
	if err != nil {
		log.Fatalf("[aloha.svc] Failed to initialize Aloha Service: %v", err)
	}

	if err := service.Start(); err != nil {
		log.Fatalf("[aloha.svc] Service stopped with error: %v", err)
	}
}
