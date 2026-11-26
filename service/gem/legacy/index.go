package legacy

import (
	"bytes"
	"fmt"

	"pttbbs/gem/storage"
)

// The table of contents of a gem area, as util/mandex wrote it to .index
// (the entry of the area that links to .index shows it). Bytes from
// util/mandex.c: Big5 and ANSI colors.
var (
	indexHeader = []byte("\x1b[1;32m\xa7\xc7\xb8\xb9\t\t\t\xba\xeb\xb5\xd8\xb0\xcf\xa5D\xc3D\x1b[m\n" +
		"\x1b[36m" + string(bytes.Repeat([]byte("\xa2w"), 38)) + "\x1b[m\n")
	indexColors = []string{"\x1b[1;33m", "\x1b[1;32m", "\x1b[1;36m", "\x1b[1;37m"}
)

// IndexMaxLevel is how deep the table of contents goes (mandex's 10).
const IndexMaxLevel = 10

// IndexMaxBytes bounds the table of contents (mandex made 12 GB of one).
const IndexMaxBytes = 16 << 20

// Index renders the table of contents of the gem area root as mandex did:
// every entry numbered by its place, below the directories that are neither
// hidden nor for moderators only, down to IndexMaxLevel levels, cut at
// IndexMaxBytes.
//
// It reads one snapshot and is cut at IndexMaxBytes, so it is made when
// read. mandex's 12 GB came from following links to parent directories.
func Index(st *storage.Store, root string) ([]byte, error) {
	v := st.View()
	defer v.Close()
	top, err := v.List(root, nil)
	if err != nil {
		return nil, err
	}
	var out bytes.Buffer
	out.Write(indexHeader)
	var walk func(l *storage.Listing, level int, numHeader []byte) error
	walk = func(l *storage.Listing, level int, numHeader []byte) error {
		for i := range l.Entries {
			if out.Len() > IndexMaxBytes {
				return nil
			}
			e := &l.Entries[i]
			name := e.Name()
			if name == "" {
				continue
			}
			prefix := numHeader
			if n := 11 * level; len(prefix) > n {
				prefix = prefix[:n]
			}
			var line bytes.Buffer
			line.Write(prefix)
			line.WriteString(indexColors[level%4])
			fmt.Fprintf(&line, "%3d. ", i+1)
			line.Write(e.Header.Title())
			line.WriteString(" \x1b[m\n")
			out.Write(line.Bytes())
			if e.Kind == storage.KindDir && level < IndexMaxLevel &&
				e.Header.Filemode()&(storage.FileBM|storage.FileHide) == 0 {
				sub, err := v.Dir(e)
				if err != nil {
					return err
				}
				if err := walk(sub, level+1, line.Bytes()); err != nil {
					return err
				}
			}
		}
		return nil
	}
	err = walk(top, 0, nil)
	return out.Bytes(), err
}
