package bbs

/*
#include <stdlib.h>
#include <string.h>
#include "cmbbs.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"time"
	"unsafe"

	"pttbbs/big5uao"
)

// BBSHome returns the default BBSHOME path defined in the C build environment
func BBSHome() string {
	return C.GoString(C.get_bbshome())
}

// UTF8ToBig5 converts a UTF-8 string to native Big5 (UAO 2.50) byte slice using pure Go UAO 2.50
func UTF8ToBig5(s string) ([]byte, error) {
	return big5uao.Encode(s), nil
}

// Big5ToUTF8 converts a Big5 byte slice (UAO 2.50) to a Go UTF-8 string using pure Go UAO 2.50
func Big5ToUTF8(b []byte) string {
	return big5uao.Decode(b)
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

// GetBoardNUser returns the active online user count for a 1-indexed bid from SHM->bcache
func (c *SHMClient) GetBoardNUser(bid int) int {
	if c == nil || bid <= 0 {
		return 0
	}
	return int(C.get_board_nuser(C.int(bid)))
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

// UtmpUpdate recalculates online users, board online counts, and hotboards in SHM
func (c *SHMClient) UtmpUpdate() {
	if c == nil {
		return
	}
	C.utmp_update()
}

// ResetUtmpBusystate sets SHM->UTMPbusystate = 0
func (c *SHMClient) ResetUtmpBusystate() {
	if c == nil {
		return
	}
	C.reset_utmp_busystate()
}

// GetUtmpBusystate returns SHM->UTMPbusystate
func (c *SHMClient) GetUtmpBusystate() int {
	if c == nil {
		return 0
	}
	return int(C.get_utmp_busystate())
}

// SetUtmpBusystate sets SHM->UTMPbusystate
func (c *SHMClient) SetUtmpBusystate(val int) {
	if c == nil {
		return
	}
	C.set_utmp_busystate(C.int(val))
}

// GetUtmpNeedUpdate returns SHM->UTMPneedupdate
func (c *SHMClient) GetUtmpNeedUpdate() int {
	if c == nil {
		return 0
	}
	return int(C.get_utmp_needupdate())
}

// SetUtmpNeedUpdate sets SHM->UTMPneedupdate
func (c *SHMClient) SetUtmpNeedUpdate(val int) {
	if c == nil {
		return
	}
	C.set_utmp_needupdate(C.int(val))
}

// GetUtmpNumber returns SHM->UTMPnumber
func (c *SHMClient) GetUtmpNumber() int {
	if c == nil {
		return 0
	}
	return int(C.get_utmp_number())
}

type UtmpStatus struct {
	Uptime     time.Time
	Number     int
	Busystate  int
	NeedUpdate int
}

// GetUtmpStatus returns status of UTMP
func (c *SHMClient) GetUtmpStatus() UtmpStatus {
	if c == nil {
		return UtmpStatus{}
	}
	var cUptime C.long
	var cNumber, cBusystate, cNeedupdate C.int
	C.get_utmp_status(&cUptime, &cNumber, &cBusystate, &cNeedupdate)
	return UtmpStatus{
		Uptime:     time.Unix(int64(cUptime), 0),
		Number:     int(cNumber),
		Busystate:  int(cBusystate),
		NeedUpdate: int(cNeedupdate),
	}
}

// PurgeUtmpSlot purges an online session in SHM slot atomically
func (c *SHMClient) PurgeUtmpSlot(slot int) {
	if c == nil || slot < 0 {
		return
	}
	C.purge_utmp_slot(C.int(slot))
}

// FixUtmpUserTable fixes circular or invalid references in SHM utmp_user table
func (c *SHMClient) FixUtmpUserTable() int {
	if c == nil {
		return 0
	}
	return int(C.fix_utmp_user_table())
}

// RebuildUtmpUser rebuilds the utmp_user table from active sessions
func (c *SHMClient) RebuildUtmpUser() {
	if c == nil {
		return
	}
	C.rebuild_utmp_user()
}

// GetHotBoardBIDs returns list of hotboard bids from SHM->HBcache
func (c *SHMClient) GetHotBoardBIDs(max int) []int {
	if c == nil || max <= 0 {
		return nil
	}
	buf := make([]C.int, max)
	n := int(C.get_hotboards(&buf[0], C.int(max)))
	if n <= 0 {
		return nil
	}
	res := make([]int, n)
	for i := 0; i < n; i++ {
		res[i] = int(buf[i])
	}
	return res
}

type UtmpCandidate struct {
	Slot          int
	PID           int
	UID           int
	UserID        string
	LastAct       time.Time
	IdleSec       int
	FriendTotal   int
	Mode          int
	BrcID         int
	IsGuest       bool
	IsValidUserID bool
	UserExists    bool
}

// GetUtmpCandidates fetches candidate session information from SHM for utmpfix
func (c *SHMClient) GetUtmpCandidates() []UtmpCandidate {
	if c == nil {
		return nil
	}
	total := int(C.get_ushm_size())
	if total <= 0 {
		return nil
	}
	cCandidates := make([]C.cgo_utmp_candidate_t, total)
	var count C.int
	if C.get_utmp_candidates(&cCandidates[0], C.int(total), &count) == 0 || count <= 0 {
		return nil
	}
	n := int(count)
	res := make([]UtmpCandidate, n)
	for i := 0; i < n; i++ {
		cand := &cCandidates[i]
		res[i] = UtmpCandidate{
			Slot:          int(cand.slot),
			PID:           int(cand.pid),
			UID:           int(cand.uid),
			UserID:        C.GoString(&cand.userid[0]),
			LastAct:       time.Unix(int64(cand.lastact), 0),
			IdleSec:       int(cand.idle_sec),
			FriendTotal:   int(cand.friendtotal),
			Mode:          int(cand.mode),
			BrcID:         int(cand.brc_id),
			IsGuest:       cand.is_guest != 0,
			IsValidUserID: cand.is_userid_valid != 0,
			UserExists:    cand.user_exists != 0,
		}
	}
	return res
}
