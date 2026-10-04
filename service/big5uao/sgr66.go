package big5uao

import (
	"bytes"
	"unicode/utf8"
)

func parseSGR(buf []byte, pos int) int {
	if pos+2 >= len(buf) || buf[pos] != 0x1b || buf[pos+1] != '[' {
		return 0
	}
	k := pos + 2
	for k < len(buf) && ((buf[k] >= '0' && buf[k] <= '9') || buf[k] == ';') {
		k++
	}
	if k < len(buf) && buf[k] == 'm' {
		return (k - pos) + 1
	}
	return 0
}

// DecodeSGR66 converts Big5-UAO bytes with possible split ANSI SGR
// into SGR 66 ANSI sequences followed by valid UTF-8 characters.
func DecodeSGR66(buf []byte) string {
	var out bytes.Buffer
	prevWasSGR := false
	i := 0
	length := len(buf)

	for i < length {
		sgrLen := parseSGR(buf, i)
		if sgrLen > 0 {
			out.Write(buf[i : i+sgrLen])
			prevWasSGR = true
			i += sgrLen
			continue
		}

		if buf[i] < 0x80 {
			out.WriteByte(buf[i])
			prevWasSGR = false
			i++
			continue
		}

		hi := buf[i]
		j := i + 1
		sgrCount := 0
		for j < length {
			midLen := parseSGR(buf, j)
			if midLen == 0 {
				break
			}
			j += midLen
			sgrCount++
		}

		if j >= length {
			// Incomplete DBCS byte at end of buffer
			out.WriteByte(hi)
			i++
			continue
		}

		lo := buf[j]
		if sgrCount > 0 {
			// Merge split SGR sequences into SGR 66
			outBytes := out.Bytes()
			if prevWasSGR && len(outBytes) > 0 && outBytes[len(outBytes)-1] == 'm' {
				// Replace trailing 'm' with ';66'
				out.Truncate(out.Len() - 1)
				out.WriteString(";66")
			} else {
				out.WriteString("\x1b[66")
			}

			k := i + 1
			for k < j {
				midLen := parseSGR(buf, k)
				out.WriteString(";")
				// Parameters between \x1b[ and m
				out.Write(buf[k+2 : k+midLen-1])
				k += midLen
			}
			out.WriteString("m")
		}

		// Convert (hi, lo) to UTF-8
		b5 := (uint16(hi) << 8) | uint16(lo)
		u := b2uTable[b5]
		if u != 0 {
			var utf8Buf [4]byte
			n := utf8.EncodeRune(utf8Buf[:], rune(u))
			out.Write(utf8Buf[:n])
		} else {
			out.WriteByte(hi)
			out.WriteByte(lo)
		}

		prevWasSGR = false
		i = j + 1
	}

	return out.String()
}
