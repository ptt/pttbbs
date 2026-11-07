package svcctl

import (
	"bufio"
	"bytes"
	"fmt"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"testing"
	"time"
)

// The test binary doubles as a fake *.svc, selected by $SVCCTL_TEST_MODE:
//
//	daemonize  fork "serve" in a new session and exit 0, like the real *.svc
//	serve      listen on $SVCCTL_TEST_SOCK, answer "ping" with "pong" and
//	           remove the socket on SIGTERM
//	dirty      like serve, but exit on SIGTERM leaving the socket file behind
//	fail       exit 1 without starting
const (
	envMode = "SVCCTL_TEST_MODE"
	envSock = "SVCCTL_TEST_SOCK"
)

func TestMain(m *testing.M) {
	switch os.Getenv(envMode) {
	case "":
		os.Exit(m.Run())
	case "daemonize":
		cmd := exec.Command(os.Args[0])
		cmd.Env = append(os.Environ(), envMode+"=serve")
		cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}
		if err := cmd.Start(); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		fmt.Println("fake.svc: starting in background")
		os.Exit(0)
	case "serve":
		serve(true)
	case "dirty":
		serve(false)
	case "fail":
		fmt.Fprintln(os.Stderr, "fake.svc: bad config")
		os.Exit(1)
	}
	os.Exit(2)
}

func serve(clean bool) {
	sock := os.Getenv(envSock)
	_ = os.Remove(sock)
	l, err := net.Listen("unix", sock)
	if err != nil {
		os.Exit(1)
	}
	l.(*net.UnixListener).SetUnlinkOnClose(clean)
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGTERM)
	go func() {
		<-sig
		time.Sleep(200 * time.Millisecond) // Pretend to flush state.
		if clean {
			l.Close()
		}
		os.Exit(0)
	}()
	for {
		c, err := l.Accept()
		if err != nil {
			continue
		}
		go func(c net.Conn) {
			defer c.Close()
			line, _ := bufio.NewReader(c).ReadString('\n')
			if strings.TrimSpace(line) == "ping" {
				fmt.Fprintln(c, "pong")
			}
		}(c)
	}
}

func ping(sock string) error {
	resp, err := LineRequest(sock, "ping")
	if err != nil {
		return err
	}
	if resp != "pong" {
		return fmt.Errorf("unexpected reply %q", resp)
	}
	return nil
}

func setup(t *testing.T) string {
	t.Helper()
	dir, err := os.MkdirTemp("", "svcctl") // Short: sun_path is limited.
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { os.RemoveAll(dir) })
	sock := filepath.Join(dir, "fake.svc.sock")
	t.Setenv(envSock, sock)
	return sock
}

// startServer runs a server as a child of the test, reaping it on exit.
func startServer(t *testing.T, mode string) int {
	t.Helper()
	cmd := exec.Command(os.Args[0])
	cmd.Env = append(os.Environ(), envMode+"="+mode)
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	go cmd.Wait()
	t.Cleanup(func() { _ = cmd.Process.Kill() })
	sock := os.Getenv(envSock)
	for i := 0; i < 50; i++ {
		if ping(sock) == nil {
			return cmd.Process.Pid
		}
		time.Sleep(100 * time.Millisecond)
	}
	t.Fatalf("server %s did not start", mode)
	return 0
}

func restartFake(t *testing.T, mode string) (string, error) {
	t.Helper()
	t.Setenv(envMode, mode)
	defer os.Unsetenv(envMode)
	var out bytes.Buffer
	err := Restart(Options{
		Name:    "fake.svc",
		Socket:  os.Getenv(envSock),
		Ping:    ping,
		SvcPath: os.Args[0],
		Timeout: 5 * time.Second,
		Stdout:  &out,
		Stderr:  &out,
	})
	// Stop whatever server the restart started.
	if pid, perr := ServerPID(os.Getenv(envSock)); perr == nil {
		t.Cleanup(func() { _ = syscall.Kill(pid, syscall.SIGKILL) })
	}
	return out.String(), err
}

func TestServerPID(t *testing.T) {
	sock := setup(t)
	want := startServer(t, "serve")
	got, err := ServerPID(sock)
	if err != nil || got != want {
		t.Fatalf("ServerPID = %d, %v; want %d", got, err, want)
	}
}

func TestRestart(t *testing.T) {
	sock := setup(t)
	oldPid := startServer(t, "serve")
	out, err := restartFake(t, "daemonize")
	if err != nil {
		t.Fatalf("Restart: %v\n%s", err, out)
	}
	newPid, err := ServerPID(sock)
	if err != nil {
		t.Fatalf("ServerPID after restart: %v", err)
	}
	if newPid == oldPid {
		t.Errorf("pid did not change: %d", newPid)
	}
	for _, s := range []string{"sending SIGTERM", "stopped", "starting in background", "restarted"} {
		if !strings.Contains(out, s) {
			t.Errorf("output lacks %q:\n%s", s, out)
		}
	}
}

func TestRestartRemovesStaleSocket(t *testing.T) {
	setup(t)
	startServer(t, "dirty")
	out, err := restartFake(t, "daemonize")
	if err != nil {
		t.Fatalf("Restart: %v\n%s", err, out)
	}
	if !strings.Contains(out, "removing stale") {
		t.Errorf("output lacks stale socket removal:\n%s", out)
	}
}

func TestRestartNotRunning(t *testing.T) {
	setup(t)
	out, err := restartFake(t, "daemonize")
	if err != nil {
		t.Fatalf("Restart: %v\n%s", err, out)
	}
	if !strings.Contains(out, "not running") {
		t.Errorf("output lacks 'not running':\n%s", out)
	}
}

func TestRestartFails(t *testing.T) {
	setup(t)
	startServer(t, "serve")
	out, err := restartFake(t, "fail")
	if err == nil {
		t.Fatalf("Restart succeeded:\n%s", out)
	}
	if !strings.Contains(out, "restart FAILED") {
		t.Errorf("output lacks failure:\n%s", out)
	}
}

func TestMainArgs(t *testing.T) {
	sock := setup(t)
	t.Setenv(envMode, "daemonize")
	for _, tc := range []struct {
		args []string
		code int
	}{
		{[]string{"-timeout", "5s", "-x", "y"}, 0},
		{[]string{"-timeout=5s", "--", "-timeout", "1s"}, 0},
		{[]string{"-timeout"}, 2},
		{[]string{"-timeout", "soon"}, 2},
	} {
		var out bytes.Buffer
		code := Main(Options{
			Name:    "fake.svc",
			Socket:  sock,
			Ping:    ping,
			SvcPath: os.Args[0],
			Stdout:  &out,
			Stderr:  &out,
		}, tc.args)
		if code != tc.code {
			t.Errorf("Main(%q) = %d, want %d\n%s", tc.args, code, tc.code, out.String())
		}
	}
	if pid, err := ServerPID(sock); err == nil {
		_ = syscall.Kill(pid, syscall.SIGKILL)
	}
}
