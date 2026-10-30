package model

import (
	"fmt"
	"strconv"
)

// PersonID identifies a user (a Lemmy person) for real: user_t in
// include/pttstruct.h ({ int unum; time4_t firstlogin; }) as one 64-bit
// integer with the same bits as user_t in (little-endian) memory, i.e.
// firstlogin << 32 | unum (person_id_pack() in include/cmbbs.h). unum is the
// slot of the account in .PASSWDS, and firstlogin tells apart the accounts
// that have used the same slot. 0 (user_t { 0, 0 }) means unknown (e.g.
// system posts, or legacy records whose author cannot be resolved any more).
//
// Every record that post.svc keeps about what a user did has both the name
// shown for the user (which may be changed, e.g. for anonymous posts) and
// this PersonID.
type PersonID uint64

// MakePersonID returns the PersonID of the account in slot usernum created at
// firstlogin.
func MakePersonID(usernum int, firstlogin uint32) PersonID {
	if usernum <= 0 {
		return 0
	}
	return PersonID(uint64(firstlogin)<<32 | uint64(uint32(usernum)))
}

// Usernum returns the slot of the account in .PASSWDS (user_t.unum).
func (p PersonID) Usernum() int { return int(int32(uint32(p))) }

// Firstlogin returns the creation time of the account (user_t.firstlogin).
func (p PersonID) Firstlogin() uint32 { return uint32(uint64(p) >> 32) }

func (p PersonID) String() string { return fmt.Sprintf("%d", uint64(p)) }

// ParsePersonID parses a PersonID in decimal, as in IPC and RFC 822 headers.
func ParsePersonID(s string) PersonID {
	v, _ := strconv.ParseUint(s, 10, 64)
	return PersonID(v)
}
