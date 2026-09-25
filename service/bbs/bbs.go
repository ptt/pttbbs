package bbs

/*
#include <stdlib.h>
#include <string.h>
#include "cmbbs.h"
#include "cmsys.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"unsafe"
)

// BBSHome returns the default BBSHOME path defined in the C build environment
func BBSHome() string {
	return C.GoString(C.get_bbshome())
}

// UTF8ToBig5 converts a UTF-8 string to native Big5 (UAO 2.50) byte slice using libcmsys.a utf8_to_big5
func UTF8ToBig5(s string) ([]byte, error) {
	if s == "" {
		return []byte{}, nil
	}

	cStr := C.CString(s)
	defer C.free(unsafe.Pointer(cStr))

	bufLen := len(s)*2 + 1
	buf := make([]byte, bufLen)

	C.utf8_to_big5(cStr, (*C.char)(unsafe.Pointer(&buf[0])), C.size_t(bufLen))

	return C.GoBytes(unsafe.Pointer(&buf[0]), C.int(C.strlen((*C.char)(unsafe.Pointer(&buf[0]))))), nil
}

// Big5ToUTF8 converts a Big5 byte slice (UAO 2.50) to a Go UTF-8 string using libcmsys.a big5_to_utf8
func Big5ToUTF8(b []byte) string {
	if len(b) == 0 {
		return ""
	}

	bufLen := len(b)*3 + 1
	buf := make([]byte, bufLen)

	C.big5_to_utf8((*C.char)(unsafe.Pointer(&b[0])), (*C.char)(unsafe.Pointer(&buf[0])), C.size_t(bufLen))

	return C.GoString((*C.char)(unsafe.Pointer(&buf[0])))
}

type SHMClient struct{}

// AttachSHM attaches to the PTT BBS Shared Memory directly via libcmbbs.a attach_check_SHM()
func AttachSHM() (*SHMClient, error) {
	ptr := C.attach_check_SHM()
	if ptr == nil {
		return nil, errors.New("failed to attach to PTT BBS SHM")
	}
	return &SHMClient{}, nil
}

// GetUserID resolves a 1-indexed UID (unum) to userid string
func (c *SHMClient) GetUserID(uid int) (string, error) {
	cStr := C.get_userid_by_uid(C.int(uid))
	if cStr == nil {
		return "", errors.New("invalid UID range")
	}
	return C.GoString(cStr), nil
}

// GetUID resolves a userid string to its 1-indexed UID (unum), or 0 if not found
func (c *SHMClient) GetUID(userid string) int {
	if c == nil || userid == "" {
		return 0
	}
	cStr := C.CString(userid)
	defer C.free(unsafe.Pointer(cStr))
	return int(C.get_uid_by_userid(cStr))
}


// SetSessionFriends updates SHM->uinfo[sid].friend_online[] and friendtotal as the single writer
func (c *SHMClient) SetSessionFriends(sid int, expectedPID int, expectedUID int, entries []uint32) bool {
	if c == nil || sid < 0 {
		return false
	}
	var ptr *C.uint
	if len(entries) > 0 {
		ptr = (*C.uint)(unsafe.Pointer(&entries[0]))
	}
	res := C.set_online_session_friends(
		C.int(sid),
		C.int(expectedPID),
		C.int(expectedUID),
		ptr,
		C.int(len(entries)),
	)
	return res != 0
}

type SessionDetail struct {
	SID          int
	PID          int
	UID          int
	UserID       string
	SyncedBySvc  bool
	FriendTotal  int
	FriendOnline []uint32
}

// GetSessionDetail reads live session details and friend_online sync source from SHM->uinfo[sid]
func (c *SHMClient) GetSessionDetail(sid int) (SessionDetail, bool) {
	if c == nil || sid < 0 {
		return SessionDetail{}, false
	}
	var cPID, cUID, cFriendSvc, cFriendTotal C.int
	var cUserID [128]C.char
	var cOnline [512]C.uint

	res := C.get_online_session_detail(
		C.int(sid),
		&cPID,
		&cUID,
		&cUserID[0],
		&cFriendSvc,
		&cFriendTotal,
		&cOnline[0],
		C.int(len(cOnline)),
	)
	if res == 0 {
		return SessionDetail{}, false
	}
	ft := int(cFriendTotal)
	if ft > len(cOnline) {
		ft = len(cOnline)
	}
	var online []uint32
	if ft > 0 {
		online = make([]uint32, ft)
		for i := 0; i < ft; i++ {
			online[i] = uint32(cOnline[i])
		}
	}
	return SessionDetail{
		SID:          sid,
		PID:          int(cPID),
		UID:          int(cUID),
		UserID:       C.GoString(&cUserID[0]),
		SyncedBySvc:  cFriendSvc != 0,
		FriendTotal:  int(cFriendTotal),
		FriendOnline: online,
	}, true
}


// IsAlohaSvcEnabled returns true if SHM->GV2.e.aloha_svc is non-zero
func (c *SHMClient) IsAlohaSvcEnabled() bool {
	if c == nil {
		return false
	}
	return C.is_aloha_svc_enabled() != 0
}

// SendAlohaMessage sends an Aloha notification waterball to a target online session in SHM
func (c *SHMClient) SendAlohaMessage(sid int, toPID int, fromPID int, fromID string) error {
	if c == nil {
		return errors.New("null SHM client")
	}

	cFrom := C.CString(fromID)
	defer C.free(unsafe.Pointer(cFrom))

	res := C.send_aloha_message(C.int(sid), C.pid_t(toPID), C.pid_t(fromPID), cFrom)
	if res != 0 {
		var reason string
		switch res {
		case -1:
			reason = fmt.Sprintf("invalid sid %d", sid)
		case -2:
			reason = fmt.Sprintf("sid %d is inactive", sid)
		case -3:
			reason = fmt.Sprintf("sid %d message queue full", sid)
		case -4:
			reason = fmt.Sprintf("sid %d pid mismatch (expected %d)", sid, toPID)
		default:
			reason = fmt.Sprintf("kill signal USR2 failed (code %d)", res)
		}
		return fmt.Errorf("failed to send aloha message to sid %d: %s", sid, reason)
	}
	return nil
}

// GetOnlineSessions returns active online session details from SHM
type OnlineSession struct {
	SID    int
	PID    int
	UID    int
	UserID string
}

func (c *SHMClient) GetOnlineSessions() []OnlineSession {
	var sessions []OnlineSession
	if c == nil {
		return sessions
	}

	total := int(C.get_ushm_size())
	for i := 0; i < total; i++ {
		var pid, uid C.int
		var cBuf [128]C.char
		if C.get_online_session(C.int(i), &pid, &uid, &cBuf[0]) != 0 {
			userID := C.GoString(&cBuf[0])
			sessions = append(sessions, OnlineSession{
				SID:    i,
				PID:    int(pid),
				UID:    int(uid),
				UserID: userID,
			})
		}
	}
	return sessions
}

type BoardInfo struct {
	BID     int
	BrdName string
	BrdAttr uint32
}

// GetBoardByBID returns board metadata for a 1-indexed bid from SHM->bcache
func (c *SHMClient) GetBoardByBID(bid int) (BoardInfo, bool) {
	if c == nil || bid <= 0 {
		return BoardInfo{}, false
	}
	var cName [128]C.char
	var cAttr C.uint
	if C.get_board_info(C.int(bid), &cName[0], &cAttr) == 0 {
		return BoardInfo{}, false
	}
	return BoardInfo{
		BID:     bid,
		BrdName: C.GoString(&cName[0]),
		BrdAttr: uint32(cAttr),
	}, true
}

// GetBoardByName resolves a board name to its 1-indexed BoardInfo from SHM->bcache
func (c *SHMClient) GetBoardByName(brdName string) (BoardInfo, bool) {
	if c == nil || brdName == "" {
		return BoardInfo{}, false
	}
	cStr := C.CString(brdName)
	defer C.free(unsafe.Pointer(cStr))
	bid := int(C.get_board_bid(cStr))
	if bid <= 0 {
		return BoardInfo{}, false
	}
	return c.GetBoardByBID(bid)
}

// GetBoards returns all valid boards from SHM->bcache
func (c *SHMClient) GetBoards() []BoardInfo {
	if c == nil {
		return nil
	}
	maxB := int(C.get_max_board())
	numB := int(C.get_num_boards())
	if numB > 0 && numB < maxB {
		maxB = numB
	}
	var boards []BoardInfo
	for bid := 1; bid <= maxB; bid++ {
		if b, ok := c.GetBoardByBID(bid); ok {
			boards = append(boards, b)
		}
	}
	return boards
}

// GetHBFLGeneration returns the current HBFL generation counter in SHM
func (c *SHMClient) GetHBFLGeneration() int {
	if c == nil {
		return 0
	}
	return int(C.get_hbfl_generation())
}

// BumpHBFLGeneration atomically increments and returns the HBFL generation counter in SHM
func (c *SHMClient) BumpHBFLGeneration() int {
	if c == nil {
		return 0
	}
	return int(C.bump_hbfl_generation())
}

// SetHBFLGeneration monotonically sets the HBFL generation counter in SHM
func (c *SHMClient) SetHBFLGeneration(gen int) {
	if c == nil {
		return
	}
	C.set_hbfl_generation(C.int(gen))
}

