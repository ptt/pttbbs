//go:build without_uao

package main

import (
	"bytes"
	"io"

	"golang.org/x/text/encoding/traditionalchinese"
	"golang.org/x/text/transform"
)

func decodeBig5Raw(b []byte) string {
	r := transform.NewReader(bytes.NewReader(b), traditionalchinese.Big5.NewDecoder())
	decoded, err := io.ReadAll(r)
	if err != nil {
		return string(b)
	}
	return string(decoded)
}

func encodeToBig5Raw(utf8Str string) []byte {
	encoder := traditionalchinese.Big5.NewEncoder()
	big5Bytes, err := encoder.Bytes([]byte(utf8Str))
	if err != nil {
		return []byte(utf8Str)
	}
	return big5Bytes
}
