package model

import (
	"fmt"
	"strconv"
	"strings"
)

// PTT article IDs (AIDC, shown as "#<8 characters>") encode the post file
// name in base 64, as common/bbs/aids.c does: bit 44 is the type (M or G),
// bits 43..12 the time stamp and bits 11..0 the hex suffix of
// "<M|G>.<stamp>.A.<hex>".
const aidAlphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_"

const aidTypeG = uint64(1) << 44

// AIDToPostFile returns the post file name that the article ID aid (without
// '#') stands for, as aidc2aidu() and aidu2fn() do.
func AIDToPostFile(aid string) (string, bool) {
	if len(aid) != 8 {
		return "", false
	}
	var v uint64
	for i := 0; i < len(aid); i++ {
		d := strings.IndexByte(aidAlphabet, aid[i])
		if d < 0 {
			return "", false
		}
		v = v<<6 | uint64(d)
	}
	stamp := (v >> 12) & 0xffffffff
	if stamp == 0 {
		return "", false
	}
	prefix := "M"
	if v&aidTypeG != 0 {
		prefix = "G"
	}
	return fmt.Sprintf("%s.%d.A.%03X", prefix, stamp, v&0xfff), true
}

// PostFileToAID returns the article ID of the post file fn, as fn2aidu()
// and aidu2aidc() do; a name without a hex suffix ("M.<stamp>.A") has 0.
func PostFileToAID(fn string) (string, bool) {
	if len(fn) < 3 || (fn[0] != 'M' && fn[0] != 'G') || fn[1] != '.' {
		return "", false
	}
	parts := strings.Split(fn[2:], ".")
	if len(parts) < 2 || parts[1] != "A" {
		return "", false
	}
	stamp, err := strconv.ParseUint(parts[0], 10, 32)
	if err != nil {
		return "", false
	}
	var hex uint64
	if len(parts) >= 3 {
		if hex, err = strconv.ParseUint(parts[2], 16, 64); err != nil {
			return "", false
		}
	}
	v := stamp<<12 | hex&0xfff
	if fn[0] == 'G' {
		v |= aidTypeG
	}
	var b [8]byte
	for i := 7; i >= 0; i-- {
		b[i] = aidAlphabet[v&0x3f]
		v >>= 6
	}
	return string(b[:]), true
}
