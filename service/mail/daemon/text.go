package daemon

import (
	"unicode/utf8"

	"pttbbs/big5uao"
)

// codec converts between UTF-8 (used inside the service) and the encodings
// of the BBS: files on disk (storage) and strings in memory (MB), e.g.
// the titles in fileheader_t. Each is Big5 (UAO), or UTF-8 that needs no
// conversion.
type codec struct {
	storageBig5 bool
	mbBig5      bool
}

// toStorage converts s to the encoding of files on disk.
func (c codec) toStorage(s string) []byte {
	if c.storageBig5 {
		return big5uao.Encode(s)
	}
	return []byte(s)
}

// fromStorage converts b from the encoding of files on disk.
func (c codec) fromStorage(b []byte) string {
	if c.storageBig5 {
		return big5uao.Decode(b)
	}
	return string(b)
}

// fromStorageArticle converts an article from the encoding of files on
// disk, keeping the characters with two colors.
func (c codec) fromStorageArticle(b []byte) string {
	if c.storageBig5 {
		return big5uao.Decode(joinSplitBig5(b))
	}
	return string(b)
}

// toMB converts s to the encoding of strings in memory.
func (c codec) toMB(s string) []byte {
	if c.mbBig5 {
		return big5uao.Encode(s)
	}
	return []byte(s)
}

// fromMB converts b from the encoding of strings in memory.
func (c codec) fromMB(b []byte) string {
	if c.mbBig5 {
		return big5uao.Decode(b)
	}
	return string(b)
}

// truncateMB cuts b (in the memory encoding) to at most n bytes without
// leaving half of a character.
func (c codec) truncateMB(b []byte, n int) []byte {
	if len(b) <= n {
		return b
	}
	if !c.mbBig5 {
		for n > 0 && !utf8.RuneStart(b[n]) {
			n--
		}
		return b[:n]
	}
	i := 0
	for i < n {
		w := 1
		if b[i] >= 0x80 {
			w = 2
		}
		if i+w > n {
			break
		}
		i += w
	}
	return b[:i]
}

// truncate cuts s so it takes at most n bytes in the memory encoding.
func (c codec) truncate(s string, n int) string {
	return c.fromMB(c.truncateMB(c.toMB(s), n))
}
