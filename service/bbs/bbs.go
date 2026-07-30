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
