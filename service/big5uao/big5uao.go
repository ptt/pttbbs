package big5uao

import (
	"strings"
	"unicode/utf8"

	"golang.org/x/text/encoding"
	"golang.org/x/text/transform"
)

// Big5UAO is the Big5-UAO (Unicode At-on 2.50) encoding implementing encoding.Encoding.
var Big5UAO encoding.Encoding = &uaoEncoding{}

// Encoding is an alias for Big5UAO.
var Encoding = Big5UAO

type uaoEncoding struct{}

func (uaoEncoding) NewDecoder() *encoding.Decoder {
	return &encoding.Decoder{Transformer: uaoDecoder{}}
}

func (uaoEncoding) NewEncoder() *encoding.Encoder {
	return &encoding.Encoder{Transformer: uaoEncoder{}}
}

func (uaoEncoding) String() string {
	return "Big5-UAO"
}

// uaoDecoder implements transform.Transformer for decoding Big5-UAO to UTF-8.
type uaoDecoder struct{ transform.NopResetter }

func (uaoDecoder) Transform(dst, src []byte, atEOF bool) (nDst, nSrc int, err error) {
	for nSrc < len(src) {
		c := src[nSrc]
		if c < 0x80 {
			if nDst >= len(dst) {
				err = transform.ErrShortDst
				break
			}
			dst[nDst] = c
			nDst++
			nSrc++
			continue
		}

		// DBCS lead byte >= 0x80
		if nSrc+1 >= len(src) {
			if !atEOF {
				err = transform.ErrShortSrc
				break
			}
			// Incomplete lead byte at EOF
			if nDst >= len(dst) {
				err = transform.ErrShortDst
				break
			}
			dst[nDst] = '?'
			nDst++
			nSrc++
			break
		}

		c2 := src[nSrc+1]
		var r rune
		var advanceSrc int
		if c2 >= 0x40 {
			b5 := (uint16(c) << 8) | uint16(c2)
			u := b2uTable[b5]
			if u == 0 {
				r = '?'
			} else {
				r = rune(u)
			}
			advanceSrc = 2
		} else {
			r = '?'
			advanceSrc = 1
		}

		rlen := utf8.RuneLen(r)
		if rlen < 0 {
			rlen = 1
			r = '?'
		}
		if nDst+rlen > len(dst) {
			err = transform.ErrShortDst
			break
		}
		nDst += utf8.EncodeRune(dst[nDst:], r)
		nSrc += advanceSrc
	}
	return nDst, nSrc, err
}

// uaoEncoder implements transform.Transformer for encoding UTF-8 to Big5-UAO.
type uaoEncoder struct{ transform.NopResetter }

func (uaoEncoder) Transform(dst, src []byte, atEOF bool) (nDst, nSrc int, err error) {
	for nSrc < len(src) {
		c := src[nSrc]
		if c < 0x80 {
			if nDst >= len(dst) {
				err = transform.ErrShortDst
				break
			}
			dst[nDst] = c
			nDst++
			nSrc++
			continue
		}

		r, size := utf8.DecodeRune(src[nSrc:])
		if r == utf8.RuneError && size == 1 {
			if !atEOF && !utf8.FullRune(src[nSrc:]) {
				err = transform.ErrShortSrc
				break
			}
			// Invalid UTF-8 sequence
			if nDst >= len(dst) {
				err = transform.ErrShortDst
				break
			}
			dst[nDst] = '?'
			nDst++
			nSrc++
			continue
		}

		var b5 uint16
		if r >= 0 && r < 0x10000 {
			b5 = u2bTable[r]
		}
		if b5 == 0 {
			b5 = '?'
		}

		if b5 > 0xFF {
			if nDst+2 > len(dst) {
				err = transform.ErrShortDst
				break
			}
			dst[nDst] = byte(b5 >> 8)
			dst[nDst+1] = byte(b5 & 0xFF)
			nDst += 2
		} else {
			if nDst >= len(dst) {
				err = transform.ErrShortDst
				break
			}
			dst[nDst] = byte(b5)
			nDst++
		}
		nSrc += size
	}
	return nDst, nSrc, err
}

// Decode converts Big5-UAO byte slice to a Go UTF-8 string (zero-allocation fast path).
func Decode(b []byte) string {
	if len(b) == 0 {
		return ""
	}
	var sb strings.Builder
	sb.Grow(len(b) * 2)

	for i := 0; i < len(b); {
		c := b[i]
		if c < 0x80 {
			sb.WriteByte(c)
			i++
			continue
		}
		if i+1 >= len(b) || b[i+1] == 0 {
			break
		}
		c2 := b[i+1]
		if c2 >= 0x40 {
			b5 := (uint16(c) << 8) | uint16(c2)
			i += 2
			u := b2uTable[b5]
			sb.WriteRune(rune(u))
		} else {
			i++
			sb.WriteByte('?')
		}
	}
	return sb.String()
}

// DecodeString converts Big5-UAO string to a Go UTF-8 string.
func DecodeString(s string) string {
	return Decode([]byte(s))
}

// Encode converts a Go UTF-8 string to Big5-UAO byte slice (fast path).
func Encode(s string) []byte {
	if len(s) == 0 {
		return []byte{}
	}
	out := make([]byte, 0, len(s))
	for i := 0; i < len(s); {
		r, size := utf8.DecodeRuneInString(s[i:])
		if r == utf8.RuneError && size == 1 {
			out = append(out, '?')
			i++
			continue
		}
		i += size

		var b5 uint16
		if r >= 0 && r < 0x10000 {
			b5 = u2bTable[r]
		}
		if b5 == 0 {
			b5 = '?'
		}
		if b5 > 0xFF {
			out = append(out, byte(b5>>8), byte(b5&0xFF))
		} else {
			out = append(out, byte(b5))
		}
	}
	return out
}

// EncodeString converts a Go UTF-8 string to Big5-UAO string.
func EncodeString(s string) string {
	return string(Encode(s))
}

// Big5ToUTF8 is an alias for Decode.
func Big5ToUTF8(b []byte) string {
	return Decode(b)
}

// UTF8ToBig5 is an alias for Encode.
func UTF8ToBig5(s string) ([]byte, error) {
	return Encode(s), nil
}
