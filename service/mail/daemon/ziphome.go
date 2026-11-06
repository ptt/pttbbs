package daemon

import (
	"archive/tar"
	"bufio"
	"compress/gzip"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"
)

const (
	zipQueueSize    = 100
	zipExcludeFile  = "etc/ziphome.exclude"
	defaultZipLimit = 25 << 20 // the attachment limit of Gmail
)

var (
	validHomeID   = regexp.MustCompile(`^[A-Za-z][A-Za-z0-9]{1,11}$`)
	errBusy       = errors.New("busy")
	errTooLarge   = errors.New("too large")
	errQueued     = errors.New("already queued")
	errNoSuchUser = errors.New("no such user")
)

type zipJob struct {
	user string
	rcpt string
}

// zipper packs homes, one job per user at a time.
type zipper struct {
	s       *Server
	jobs    chan zipJob
	mu      sync.Mutex
	pending map[string]bool
}

func newZipper(s *Server) *zipper {
	return &zipper{
		s:       s,
		jobs:    make(chan zipJob, zipQueueSize),
		pending: make(map[string]bool),
	}
}

func (z *zipper) start(stop <-chan struct{}) {
	for i := 0; i < z.s.cfg.ZipWorkers; i++ {
		go func() {
			for {
				select {
				case <-stop:
					return
				case job := <-z.jobs:
					z.run(job)
					z.mu.Lock()
					delete(z.pending, strings.ToLower(job.user))
					z.mu.Unlock()
				}
			}
		}()
	}
}

// add queues packing the home of user (as registered) for rcpt.
func (z *zipper) add(user, rcpt string) error {
	if !validHomeID.MatchString(user) {
		return fmt.Errorf("bad user")
	}
	addr, err := checkRcpt(rcpt)
	if err != nil {
		return err
	}
	if uid, registered, _ := z.s.bbs.LoadMailUser(user); uid < 1 || registered != user {
		return errNoSuchUser
	}
	key := strings.ToLower(user)
	z.mu.Lock()
	defer z.mu.Unlock()
	if z.pending[key] {
		return errQueued
	}
	select {
	case z.jobs <- zipJob{user: user, rcpt: addr}:
		z.pending[key] = true
		log.Printf("[mail.svc] Queued the home of %s for %s", user, addr)
		return nil
	default:
		return errBusy
	}
}

// excludes reads the patterns of files not to pack (as tar -X).
func (z *zipper) excludes() []string {
	b, err := os.ReadFile(filepath.Join(z.s.cfg.BBSHome, zipExcludeFile))
	if err != nil {
		return nil
	}
	var pats []string
	for _, l := range strings.Split(string(b), "\n") {
		if l = strings.TrimSpace(l); l != "" && !strings.HasPrefix(l, "#") {
			pats = append(pats, l)
		}
	}
	return pats
}

// excluded matches name (a path with '/') against the patterns, like tar
// does by default: a pattern may match any trailing part of the path.
func excluded(name string, pats []string) bool {
	parts := strings.Split(name, "/")
	for _, p := range pats {
		for i := range parts {
			if ok, _ := path.Match(p, strings.Join(parts[i:], "/")); ok {
				return true
			}
		}
	}
	return false
}

// limitWriter fails once more than n bytes are written.
type limitWriter struct {
	w io.Writer
	n int64
}

func (l *limitWriter) Write(p []byte) (int, error) {
	if int64(len(p)) > l.n {
		return 0, errTooLarge
	}
	l.n -= int64(len(p))
	return l.w.Write(p)
}

// pack writes the home dir as a tgz into w, with the names as
// "home/x/userid/..." like the old "tar czf - home/x/userid".
func pack(w io.Writer, bbsHome, rel string, pats []string) error {
	gz := gzip.NewWriter(w)
	tw := tar.NewWriter(gz)
	root := filepath.Join(bbsHome, rel)
	err := filepath.WalkDir(root, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		r, _ := filepath.Rel(bbsHome, p)
		name := filepath.ToSlash(r)
		if p != root && excluded(name, pats) {
			if d.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}
		info, err := d.Info()
		if err != nil {
			return err
		}
		link := ""
		switch {
		case info.Mode().IsRegular(), info.IsDir():
		case info.Mode()&fs.ModeSymlink != 0:
			if link, err = os.Readlink(p); err != nil {
				return nil
			}
		default: // sockets, devices, ...
			return nil
		}
		h, err := tar.FileInfoHeader(info, link)
		if err != nil {
			return err
		}
		h.Name = name
		if info.IsDir() {
			h.Name += "/"
		}
		h.Uname, h.Gname = "", ""
		if err := tw.WriteHeader(h); err != nil {
			return err
		}
		if info.Mode().IsRegular() {
			f, err := os.Open(p)
			if err != nil {
				return err
			}
			_, err = io.Copy(tw, f)
			f.Close()
			return err
		}
		return nil
	})
	if err != nil {
		return err
	}
	if err := tw.Close(); err != nil {
		return err
	}
	return gz.Close()
}

func (z *zipper) run(job zipJob) {
	cfg := z.s.cfg
	rel := path.Join("home", job.user[:1], job.user)
	tgz := filepath.Join(cfg.SpoolDir, fmt.Sprintf("home.%s.%d.tgz", job.user, time.Now().UnixNano()))
	defer os.Remove(tgz)

	err := func() error {
		if st, err := os.Stat(filepath.Join(cfg.BBSHome, rel)); err != nil || !st.IsDir() {
			return errNoSuchUser
		}
		f, err := os.Create(tgz)
		if err != nil {
			return err
		}
		bw := bufio.NewWriter(f)
		err = pack(&limitWriter{w: bw, n: cfg.ZipLimit}, cfg.BBSHome, rel, z.excludes())
		if err == nil {
			err = bw.Flush()
		}
		if cerr := f.Close(); err == nil {
			err = cerr
		}
		return err
	}()

	limitMB := float64(cfg.ZipLimit) / (1 << 20)
	switch {
	case errors.Is(err, errTooLarge):
		log.Printf("[mail.svc] The home of %s is larger than %g MB", job.user, limitMB)
		z.s.notify(job.user, "私人資料打包過大，無法寄出", fmt.Sprintf(
			"您的私人資料壓縮後超過 %g MB，大部分的信箱都收不下，所以沒有寄出。\n\n"+
				"請先清理信箱或其他個人資料，再試一次。\n", limitMB))
		return
	case err != nil:
		log.Printf("[mail.svc] Pack the home of %s: %v", job.user, err)
		z.s.notify(job.user, "私人資料打包失敗", "打包您的私人資料時發生錯誤，"+
			"沒有寄出。請稍後再試，或向站長回報。\n")
		return
	}

	f, err := os.Open(tgz)
	if err != nil {
		log.Printf("[mail.svc] Open %s: %v", tgz, err)
		return
	}
	defer f.Close()
	st, _ := f.Stat()
	filename := fmt.Sprintf("home.%s.tgz", job.user)
	subject := job.user + " 私人資料"
	body := fmt.Sprintf("附件是 %s 的私人資料 (%s)，以 tar + gzip 壓縮。\n",
		job.user, filename)
	err = z.s.out.enqueue(z.s.out.sender(job.user), job.rcpt, subject,
		func(w io.Writer) error {
			return z.s.out.composeAttachment(w, job.user, job.rcpt, subject, body,
				filename, "application/gzip", f)
		})
	if err != nil {
		z.s.notify(job.user, "私人資料打包失敗", "無法寄出您的私人資料，"+
			"請稍後再試，或向站長回報。\n")
		return
	}
	log.Printf("[mail.svc] Packed the home of %s (%d bytes) for %s", job.user,
		st.Size(), job.rcpt)
}
