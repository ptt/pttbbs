package storage

import "fmt"

// Limits are the quotas of a board's gem area; 0 is no limit. Changes that
// grow a total over its limit are refused (an area over it may shrink).
// Copied trees count in each place, as users see them.
type Limits struct {
	Nodes int64 // entries in the whole area
	Dirs  int64 // directories in the whole area
	Bytes int64 // bytes of the files in the whole area
	Width int64 // entries in one directory
	Depth int64 // levels of directories
	Copy  int64 // entries a single copy may bring
}

// DefaultLimits are about 2.5 to 10 times the 99th percentile of the boards
// of PTT in 2026: 0.4% of the boards are already over one of them.
var DefaultLimits = Limits{
	Nodes: 100000,
	Dirs:  20000,
	Bytes: 1 << 30,
	Width: 5000,
	Depth: 12,
	Copy:  10000,
}

func over(limit int64, before, after uint64) bool {
	return limit > 0 && after > uint64(limit) && after > before
}

// check refuses after when it breaks a limit that before did not, or grows
// a total already over its limit.
func (lim Limits) check(before, after Totals) error {
	switch {
	case over(lim.Nodes, before.Nodes(), after.Nodes()):
		return errorf(ErrQuota, "nodes %d > %d", after.Nodes(), lim.Nodes)
	case over(lim.Dirs, before.Dirs, after.Dirs):
		return errorf(ErrQuota, "dirs %d > %d", after.Dirs, lim.Dirs)
	case over(lim.Bytes, before.Bytes, after.Bytes):
		return errorf(ErrQuota, "bytes %d > %d", after.Bytes, lim.Bytes)
	case over(lim.Depth, before.Depth, after.Depth):
		return errorf(ErrQuota, "depth %d > %d", after.Depth, lim.Depth)
	}
	return nil
}

func (lim Limits) checkWidth(n int) error {
	if lim.Width > 0 && int64(n) > lim.Width {
		return errorf(ErrQuota, "width %d > %d", n, lim.Width)
	}
	return nil
}

func (lim Limits) checkCopy(t Totals) error {
	if lim.Copy > 0 && int64(t.Nodes()) > lim.Copy {
		return errorf(ErrQuota, "copy %d > %d", t.Nodes(), lim.Copy)
	}
	return nil
}

func (lim Limits) String() string {
	return fmt.Sprintf("nodes=%d dirs=%d bytes=%d width=%d depth=%d copy=%d",
		lim.Nodes, lim.Dirs, lim.Bytes, lim.Width, lim.Depth, lim.Copy)
}
