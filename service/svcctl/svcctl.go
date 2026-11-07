// Package svcctl implements the "restart" command shared by the *.ctl tools.
//
// A restart:
//  1. connects to the service socket and finds the server process from the
//     peer credentials (SO_PEERCRED) of the connection;
//  2. asks the server to stop (by a Stop callback, or SIGTERM by default) and
//     waits until the socket file is gone and the process has exited;
//  3. runs the *.svc found in the same directory as the running *.ctl;
//  4. waits until the socket appears, connects and pings the new server.
//
// Any failure is reported, and Restart returns an error.
package svcctl

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
	"time"
)

// DefaultTimeout is how long each of the stop and start phases may take.
const DefaultTimeout = 60 * time.Second

const pollInterval = 100 * time.Millisecond

// Options describe how to restart one service.
type Options struct {
	// Name of the service binary, e.g. "stat.svc".
	Name string
	// Socket is the path of the UNIX socket the *.ctl talks to.
	Socket string
	// Ping checks that a (new) server listening on Socket is alive.
	Ping func(socket string) error
	// Stop optionally asks the server (pid) to shut down by itself, e.g. with
	// a command over the socket. When nil, SIGTERM is sent to pid.
	Stop func(socket string, pid int) error

	// SvcPath overrides the *.svc to run (default: Name in the directory of
	// the running executable).
	SvcPath string
	// Args are passed to the *.svc.
	Args []string
	// Timeout for each of the stop and start phases (default DefaultTimeout).
	Timeout time.Duration

	// Stdout and Stderr receive the progress (default os.Stdout, os.Stderr).
	Stdout, Stderr io.Writer
}

// Usage is the line to add to the usage text of a *.ctl.
const Usage = "restart [-timeout D] [--] [svc args...]  Stop the service, start it again and check it answers"

// Main parses the arguments after "restart", runs Restart, and returns the
// exit code. args may start with "-timeout D" (and "--"); the rest are passed
// to the *.svc.
func Main(opts Options, args []string) int {
	if opts.Stderr == nil {
		opts.Stderr = os.Stderr
	}
	opts.Timeout = DefaultTimeout
	for len(args) > 0 {
		if !strings.HasPrefix(args[0], "-") {
			break
		}
		if args[0] == "--" {
			args = args[1:]
			break
		}
		name, value, hasValue := strings.Cut(strings.TrimLeft(args[0], "-"), "=")
		if name == "h" || name == "help" {
			fmt.Fprintf(opts.Stderr, "Usage: %s\n  -timeout D: time limit for each of the stop and start phases (default %v)\n", Usage, DefaultTimeout)
			return 2
		}
		if name != "timeout" {
			break // An argument for the *.svc.
		}
		args = args[1:]
		if !hasValue {
			if len(args) == 0 {
				fmt.Fprintf(opts.Stderr, "%s restart: -timeout needs a value\n", opts.Name)
				return 2
			}
			value, args = args[0], args[1:]
		}
		d, err := time.ParseDuration(value)
		if err != nil || d <= 0 {
			fmt.Fprintf(opts.Stderr, "%s restart: invalid -timeout %q\n", opts.Name, value)
			return 2
		}
		opts.Timeout = d
	}
	opts.Args = append(opts.Args, args...)
	if err := Restart(opts); err != nil {
		return 1
	}
	return 0
}

// Restart restarts the service described by opts.
func Restart(opts Options) error {
	if opts.Stdout == nil {
		opts.Stdout = os.Stdout
	}
	if opts.Stderr == nil {
		opts.Stderr = os.Stderr
	}
	err := restart(&opts)
	if err != nil {
		fmt.Fprintf(opts.Stderr, "%s: restart FAILED: %v\n", opts.Name, err)
	}
	return err
}

func restart(o *Options) error {
	if o.Timeout <= 0 {
		o.Timeout = DefaultTimeout
	}
	if o.Ping == nil {
		return errors.New("no ping method")
	}
	svc := o.SvcPath
	if svc == "" {
		exe, err := os.Executable()
		if err != nil {
			return fmt.Errorf("cannot find own executable: %w", err)
		}
		svc = filepath.Join(filepath.Dir(exe), o.Name)
	}
	if st, err := os.Stat(svc); err != nil {
		return fmt.Errorf("cannot find %s: %w", svc, err)
	} else if st.IsDir() || st.Mode()&0111 == 0 {
		return fmt.Errorf("%s is not executable", svc)
	}

	oldPid, err := stop(o)
	if err != nil {
		return err
	}

	start := time.Now()
	fmt.Fprintf(o.Stdout, "%s: starting %s\n", o.Name, svc)
	cmd := exec.Command(svc, o.Args...)
	cmd.Stdout = o.Stdout
	cmd.Stderr = o.Stderr
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("cannot run %s: %w", svc, err)
	}
	exited := make(chan error, 1)
	go func() { exited <- cmd.Wait() }()

	newPid, err := waitReady(o, exited, start.Add(o.Timeout))
	if err != nil {
		return err
	}
	if oldPid > 0 && newPid == oldPid {
		return fmt.Errorf("%s is still served by the old pid %d", o.Socket, oldPid)
	}
	fmt.Fprintf(o.Stdout, "%s: restarted, pid %d answers on %s (%.1fs)\n",
		o.Name, newPid, o.Socket, time.Since(start).Seconds())
	return nil
}

// stop stops the running server, if any, and returns its pid (0 if none).
func stop(o *Options) (int, error) {
	pid, err := ServerPID(o.Socket)
	if err != nil {
		if _, serr := os.Lstat(o.Socket); errors.Is(serr, os.ErrNotExist) {
			fmt.Fprintf(o.Stdout, "%s: not running (no %s)\n", o.Name, o.Socket)
			return 0, nil
		}
		if errors.Is(err, syscall.ECONNREFUSED) {
			// A socket file nobody listens on is left by a server that
			// died; the new server removes it.
			fmt.Fprintf(o.Stdout, "%s: not running (stale %s)\n", o.Name, o.Socket)
			return 0, nil
		}
		return 0, fmt.Errorf("cannot connect to %s: %w", o.Socket, err)
	}

	start := time.Now()
	if o.Stop != nil {
		fmt.Fprintf(o.Stdout, "%s: asking pid %d to shut down\n", o.Name, pid)
		err = o.Stop(o.Socket, pid)
	} else {
		fmt.Fprintf(o.Stdout, "%s: sending SIGTERM to pid %d\n", o.Name, pid)
		err = syscall.Kill(pid, syscall.SIGTERM)
	}
	if err != nil {
		return pid, fmt.Errorf("cannot stop pid %d: %w", pid, err)
	}

	deadline := start.Add(o.Timeout)
	for {
		alive := processAlive(pid)
		_, serr := os.Lstat(o.Socket)
		sockGone := errors.Is(serr, os.ErrNotExist)
		if !alive {
			if !sockGone {
				// The server did not remove its socket.
				if _, err := ServerPID(o.Socket); err == nil {
					return pid, fmt.Errorf("pid %d exited but %s is served by another process", pid, o.Socket)
				}
				fmt.Fprintf(o.Stdout, "%s: removing stale %s\n", o.Name, o.Socket)
				if err := os.Remove(o.Socket); err != nil && !errors.Is(err, os.ErrNotExist) {
					return pid, fmt.Errorf("cannot remove stale %s: %w", o.Socket, err)
				}
			}
			break
		}
		if time.Now().After(deadline) {
			if sockGone {
				return pid, fmt.Errorf("pid %d closed %s but did not exit within %v", pid, o.Socket, o.Timeout)
			}
			return pid, fmt.Errorf("pid %d did not shut down within %v", pid, o.Timeout)
		}
		time.Sleep(pollInterval)
	}
	fmt.Fprintf(o.Stdout, "%s: pid %d stopped (%.1fs)\n", o.Name, pid, time.Since(start).Seconds())
	return pid, nil
}

// waitReady waits for the new server to create the socket and answer a ping.
func waitReady(o *Options, exited <-chan error, deadline time.Time) (int, error) {
	var lastErr error
	for {
		select {
		case err := <-exited:
			if err != nil {
				return 0, fmt.Errorf("%s failed: %w", o.Name, err)
			}
			exited = nil // Daemonized; keep waiting for the socket.
		default:
		}

		if _, err := os.Lstat(o.Socket); err != nil {
			lastErr = fmt.Errorf("%s did not appear", o.Socket)
		} else if pid, err := ServerPID(o.Socket); err != nil {
			lastErr = err
		} else if err := o.Ping(o.Socket); err != nil {
			lastErr = fmt.Errorf("ping failed: %w", err)
		} else {
			return pid, nil
		}

		if time.Now().After(deadline) {
			if exited != nil {
				return 0, fmt.Errorf("%s did not daemonize, last error: %v", o.Name, lastErr)
			}
			return 0, lastErr
		}
		time.Sleep(pollInterval)
	}
}

// ServerPID connects to a UNIX socket and returns the pid of the process
// that listens on it.
func ServerPID(socket string) (int, error) {
	conn, err := net.DialTimeout("unix", socket, 2*time.Second)
	if err != nil {
		return 0, err
	}
	defer conn.Close()
	uc, ok := conn.(*net.UnixConn)
	if !ok {
		return 0, fmt.Errorf("%s: not a UNIX connection", socket)
	}
	raw, err := uc.SyscallConn()
	if err != nil {
		return 0, err
	}
	var cred *syscall.Ucred
	var credErr error
	if err := raw.Control(func(fd uintptr) {
		cred, credErr = syscall.GetsockoptUcred(int(fd), syscall.SOL_SOCKET, syscall.SO_PEERCRED)
	}); err != nil {
		return 0, err
	}
	if credErr != nil {
		return 0, fmt.Errorf("SO_PEERCRED on %s: %w", socket, credErr)
	}
	if cred.Pid <= 0 {
		return 0, fmt.Errorf("SO_PEERCRED on %s: no pid", socket)
	}
	return int(cred.Pid), nil
}

func processAlive(pid int) bool {
	err := syscall.Kill(pid, 0)
	return err == nil || errors.Is(err, syscall.EPERM)
}

// Dial connects to socket and sets a deadline for the whole exchange.
func Dial(socket string, timeout time.Duration) (net.Conn, error) {
	conn, err := net.DialTimeout("unix", socket, timeout)
	if err != nil {
		return nil, err
	}
	_ = conn.SetDeadline(time.Now().Add(timeout))
	return conn, nil
}

// PingTimeout limits one ping exchange.
const PingTimeout = 5 * time.Second

// JSONRequest sends req as one JSON value and decodes one JSON reply into
// resp, for the services speaking JSON over their socket.
func JSONRequest(socket string, req, resp any) error {
	conn, err := Dial(socket, PingTimeout)
	if err != nil {
		return err
	}
	defer conn.Close()
	if err := json.NewEncoder(conn).Encode(req); err != nil {
		return err
	}
	return json.NewDecoder(conn).Decode(resp)
}

// LineRequest sends one request line and returns the first reply line, for
// the services speaking a line protocol over their socket.
func LineRequest(socket, req string) (string, error) {
	conn, err := Dial(socket, PingTimeout)
	if err != nil {
		return "", err
	}
	defer conn.Close()
	if _, err := fmt.Fprintf(conn, "%s\n", req); err != nil {
		return "", err
	}
	line, err := bufio.NewReader(conn).ReadString('\n')
	if err != nil {
		return "", err
	}
	return strings.TrimRight(line, "\r\n"), nil
}
