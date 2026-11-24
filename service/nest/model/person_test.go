package model

import (
	"encoding/binary"
	"testing"
)

// PersonID must have the same bits as user_t { int unum; time4_t firstlogin; }
// in memory (little-endian), so that C can memcpy one into the other.
func TestPersonIDMatchesUserT(t *testing.T) {
	const unum, firstlogin = 123456, 1700000000
	var userT [8]byte
	binary.LittleEndian.PutUint32(userT[0:4], uint32(unum))
	binary.LittleEndian.PutUint32(userT[4:8], uint32(firstlogin))

	p := MakePersonID(unum, firstlogin)
	if got := PersonID(binary.LittleEndian.Uint64(userT[:])); got != p {
		t.Errorf("user_t bits %#x, PersonID %#x", uint64(got), uint64(p))
	}
	if p.Usernum() != unum || p.Firstlogin() != firstlogin {
		t.Errorf("PersonID %#x: usernum %d, firstlogin %d", uint64(p), p.Usernum(), p.Firstlogin())
	}
	if MakePersonID(0, firstlogin) != 0 {
		t.Error("unknown user must be 0")
	}
}
