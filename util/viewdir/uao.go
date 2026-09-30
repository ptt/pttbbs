package main

import (
	"pttbbs/big5uao"
)

func decodeBig5Raw(b []byte) string {
	return big5uao.Decode(b)
}

func encodeToBig5Raw(utf8Str string) []byte {
	return big5uao.Encode(utf8Str)
}
