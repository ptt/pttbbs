package importer

import "golang.org/x/sys/unix"

// fileBirthTime returns the birth time of the file at path (statx
// STATX_BTIME), or 0 where the file system does not keep one.
func fileBirthTime(path string) int64 {
	var st unix.Statx_t
	if err := unix.Statx(unix.AT_FDCWD, path, 0, unix.STATX_BTIME, &st); err != nil || st.Mask&unix.STATX_BTIME == 0 {
		return 0
	}
	return st.Btime.Sec
}
