package rotate

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"

	"github.com/klauspost/compress/zstd"
)

// compressor wraps w to compress what is written to it.
func compressor(w io.Writer, c Compression) (io.WriteCloser, error) {
	switch c.Algo {
	case "zstd":
		return zstd.NewWriter(w, zstd.WithEncoderLevel(zstd.EncoderLevelFromZstd(c.Level)))
	case "gzip":
		level := c.Level
		if level == 0 {
			level = gzip.DefaultCompression
		}
		return gzip.NewWriterLevel(w, level)
	}
	return nopCloser{w}, nil
}

type nopCloser struct{ io.Writer }

func (nopCloser) Close() error { return nil }

// decompressor reads what c wrote to r.
func decompressor(r io.Reader, c Compression) (io.ReadCloser, error) {
	switch c.Algo {
	case "zstd":
		d, err := zstd.NewReader(r)
		if err != nil {
			return nil, err
		}
		return d.IOReadCloser(), nil
	case "gzip":
		return gzip.NewReader(r)
	}
	return io.NopCloser(r), nil
}

// writeArchive writes dst (through dst.tmp) with what fill writes, and
// checks that it reads back to the same bytes. The checksum of what fill
// wrote is compared with the one of the archive read back.
func writeArchive(dst string, mode fs.FileMode, c Compression,
	fill func(io.Writer) error) error {
	tmp := dst + ".tmp"
	f, err := os.OpenFile(tmp, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, mode.Perm())
	if err != nil {
		return err
	}
	fail := func(err error) error {
		f.Close()
		os.Remove(tmp)
		return err
	}
	z, err := compressor(f, c)
	if err != nil {
		return fail(err)
	}
	sum := sha256.New()
	if err := fill(io.MultiWriter(z, sum)); err != nil {
		z.Close()
		return fail(err)
	}
	if err := z.Close(); err != nil {
		return fail(err)
	}
	if err := f.Sync(); err != nil {
		return fail(err)
	}
	if err := f.Close(); err != nil {
		os.Remove(tmp)
		return err
	}
	if err := verify(tmp, c, sum.Sum(nil)); err != nil {
		os.Remove(tmp)
		return err
	}
	if err := os.Rename(tmp, dst); err != nil {
		os.Remove(tmp)
		return err
	}
	return nil
}

func verify(path string, c Compression, want []byte) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	r, err := decompressor(f, c)
	if err != nil {
		return fmt.Errorf("%s: %w", path, err)
	}
	defer r.Close()
	sum := sha256.New()
	if _, err := io.Copy(sum, r); err != nil {
		return fmt.Errorf("%s: %w", path, err)
	}
	if !bytes.Equal(sum.Sum(nil), want) {
		return fmt.Errorf("%s: does not read back the same", path)
	}
	return nil
}

// compressFile compresses src into dst, with the mode and time of src.
func compressFile(src, dst string, c Compression) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	st, err := in.Stat()
	if err != nil {
		return err
	}
	if err := writeArchive(dst, st.Mode(), c, func(w io.Writer) error {
		_, err := io.Copy(w, in)
		return err
	}); err != nil {
		return err
	}
	return os.Chtimes(dst, st.ModTime(), st.ModTime())
}

// tarDir packs the directory dir into dst as a tar file named <name>/...:
// directories, regular files and symbolic links (others, e.g. sockets, are
// skipped). Files that vanish meanwhile are skipped too.
func tarDir(dir, name, dst string, c Compression) error {
	return writeArchive(dst, 0644, c, func(w io.Writer) error {
		tw := tar.NewWriter(w)
		err := filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
			if err != nil {
				if os.IsNotExist(err) && path != dir {
					return nil
				}
				return err
			}
			info, err := d.Info()
			if os.IsNotExist(err) {
				return nil
			} else if err != nil {
				return err
			}
			link := ""
			switch {
			case info.Mode().IsRegular(), info.IsDir():
			case info.Mode()&fs.ModeSymlink != 0:
				if link, err = os.Readlink(path); err != nil {
					return nil
				}
			default:
				return nil
			}
			hdr, err := tar.FileInfoHeader(info, link)
			if err != nil {
				return err
			}
			rel, _ := filepath.Rel(dir, path)
			hdr.Name = filepath.ToSlash(filepath.Join(name, rel))
			if info.IsDir() {
				hdr.Name += "/"
			}
			if !info.Mode().IsRegular() {
				return tw.WriteHeader(hdr)
			}
			f, err := os.Open(path)
			if os.IsNotExist(err) {
				return nil
			} else if err != nil {
				return err
			}
			defer f.Close()
			// The size in the header: a file still growing is cut there.
			if err := tw.WriteHeader(hdr); err != nil {
				return err
			}
			n, err := io.Copy(tw, io.LimitReader(f, hdr.Size))
			if err == nil && n < hdr.Size {
				// It shrank: pad, as tar needs the size in the header.
				_, err = io.CopyN(tw, zeroReader{}, hdr.Size-n)
			}
			return err
		})
		if err != nil {
			return err
		}
		return tw.Close()
	})
}

type zeroReader struct{}

func (zeroReader) Read(p []byte) (int, error) {
	for i := range p {
		p[i] = 0
	}
	return len(p), nil
}

// copyFile copies src to dst (through dst.tmp), with the mode of src.
func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	st, err := in.Stat()
	if err != nil {
		return err
	}
	tmp := dst + ".tmp"
	out, err := os.OpenFile(tmp, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, st.Mode().Perm())
	if err != nil {
		return err
	}
	if _, err := io.Copy(out, in); err != nil {
		out.Close()
		os.Remove(tmp)
		return err
	}
	if err := out.Close(); err != nil {
		os.Remove(tmp)
		return err
	}
	return os.Rename(tmp, dst)
}
