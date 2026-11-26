//go:build !linux

package importer

// fileBirthTime returns 0: only Linux (statx) is asked for birth times, so
// elsewhere the modification time stands for the creation time.
func fileBirthTime(path string) int64 { return 0 }
