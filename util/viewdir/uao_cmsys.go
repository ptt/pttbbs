//go:build !without_uao

package main

/*
#cgo CFLAGS: -I../../include
#cgo LDFLAGS: -L../../common/sys -lcmsys
#include <stdlib.h>
#include "cmsys.h"
*/
import "C"
import (
	"unsafe"
)

func decodeBig5Raw(b []byte) string {
	if len(b) == 0 {
		return ""
	}
	inBuf := make([]byte, len(b)+1)
	copy(inBuf, b)
	cInPtr := (*C.char)(unsafe.Pointer(&inBuf[0]))

	maxLen := C.size_t(len(b)*3 + 1)
	outBuf := make([]byte, maxLen)
	cOutPtr := (*C.char)(unsafe.Pointer(&outBuf[0]))

	C.big5_to_utf8(cInPtr, cOutPtr, maxLen)
	return C.GoString(cOutPtr)
}

func encodeToBig5Raw(utf8Str string) []byte {
	if len(utf8Str) == 0 {
		return nil
	}
	cIn := C.CString(utf8Str)
	defer C.free(unsafe.Pointer(cIn))

	maxLen := C.size_t(len(utf8Str)*2 + 1)
	outBuf := make([]byte, maxLen)
	cOutPtr := (*C.char)(unsafe.Pointer(&outBuf[0]))

	C.utf8_to_big5(cIn, cOutPtr, maxLen)
	return []byte(C.GoString(cOutPtr))
}
