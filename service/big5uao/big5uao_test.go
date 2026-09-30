package big5uao

import (
	"bytes"
	"io"
	"testing"

	"golang.org/x/text/transform"
)

func TestEncodingInterface(t *testing.T) {
	testText := "測試水球★☆あいうえお"

	// 1. Test NewEncoder().Bytes() and NewDecoder().Bytes()
	enc := Big5UAO.NewEncoder()
	big5Bytes, err := enc.Bytes([]byte(testText))
	if err != nil {
		t.Fatalf("Encoder.Bytes failed: %v", err)
	}

	dec := Big5UAO.NewDecoder()
	utf8Bytes, err := dec.Bytes(big5Bytes)
	if err != nil {
		t.Fatalf("Decoder.Bytes failed: %v", err)
	}
	if string(utf8Bytes) != testText {
		t.Fatalf("Encoding interface roundtrip mismatch: got %q, want %q", string(utf8Bytes), testText)
	}

	// 2. Test NewEncoder().String() and NewDecoder().String()
	encStr, err := Big5UAO.NewEncoder().String(testText)
	if err != nil {
		t.Fatalf("Encoder.String failed: %v", err)
	}
	decStr, err := Big5UAO.NewDecoder().String(encStr)
	if err != nil {
		t.Fatalf("Decoder.String failed: %v", err)
	}
	if decStr != testText {
		t.Fatalf("String roundtrip mismatch: got %q, want %q", decStr, testText)
	}

	// 3. Test Reader and Writer streaming with transform.NewReader / transform.NewWriter
	var buf bytes.Buffer
	writer := transform.NewWriter(&buf, Big5UAO.NewEncoder())
	if _, err := writer.Write([]byte(testText)); err != nil {
		t.Fatalf("Writer write failed: %v", err)
	}
	if err := writer.Close(); err != nil {
		t.Fatalf("Writer close failed: %v", err)
	}

	reader := transform.NewReader(&buf, Big5UAO.NewDecoder())
	readBack, err := io.ReadAll(reader)
	if err != nil {
		t.Fatalf("Reader read failed: %v", err)
	}
	if string(readBack) != testText {
		t.Fatalf("Stream roundtrip mismatch: got %q, want %q", string(readBack), testText)
	}
}

func TestFastPathParityWithEncodingInterface(t *testing.T) {
	texts := []string{
		"Hello, World! 12345",
		"測試水球，批踢踢實業坊",
		"★☆♠♥♦♣♪ あいうえお カキクケコ 碁銹恒",
		"\x1b[1;33m[公告]\x1b[m \x1b[1;32mGossiping\x1b[m 八卦板",
	}

	for _, text := range texts {
		fastEnc := Encode(text)
		ifaceEnc, err := Big5UAO.NewEncoder().Bytes([]byte(text))
		if err != nil {
			t.Fatalf("NewEncoder.Bytes failed on %q: %v", text, err)
		}
		if !bytes.Equal(fastEnc, ifaceEnc) {
			t.Fatalf("Encode output disparity on %q:\nFast:  %x\nIface: %x", text, fastEnc, ifaceEnc)
		}

		fastDec := Decode(fastEnc)
		ifaceDec, err := Big5UAO.NewDecoder().Bytes(fastEnc)
		if err != nil {
			t.Fatalf("NewDecoder.Bytes failed on %q: %v", text, err)
		}
		if fastDec != string(ifaceDec) {
			t.Fatalf("Decode output disparity on %q:\nFast:  %q\nIface: %q", text, fastDec, string(ifaceDec))
		}
		if fastDec != text {
			t.Fatalf("Roundtrip failed on %q: got %q", text, fastDec)
		}
	}
}

func TestEdgeCases(t *testing.T) {
	if Decode(nil) != "" {
		t.Errorf("Decode(nil) should be empty string")
	}
	if len(Encode("")) != 0 {
		t.Errorf("Encode(\"\") should be empty byte slice")
	}

	// Truncated trailing byte
	truncated := []byte{0xa4}
	if dec := Decode(truncated); dec != "" {
		t.Errorf("Decode truncated lead byte: expected empty, got %q", dec)
	}

	// Invalid trailing byte (< 0x40)
	invalidTrail := []byte{0xa4, 0x20}
	if dec := Decode(invalidTrail); dec != "? " {
		t.Errorf("Decode invalid trail: expected \"? \", got %q", dec)
	}
}

func BenchmarkFastPathDecode(b *testing.B) {
	text := "推 jack86326: term.ptt.cc也有此現象，control+P 發文也有部分破圖    09/27 19:04"
	enc := Encode(text)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = Decode(enc)
	}
}

func BenchmarkFastPathEncode(b *testing.B) {
	text := "推 jack86326: term.ptt.cc也有此現象，control+P 發文也有部分破圖    09/27 19:04"
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = Encode(text)
	}
}

func BenchmarkEncodingInterfaceDecode(b *testing.B) {
	text := "推 jack86326: term.ptt.cc也有此現象，control+P 發文也有部分破圖    09/27 19:04"
	enc := Encode(text)
	dec := Big5UAO.NewDecoder()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_, _ = dec.Bytes(enc)
	}
}

func BenchmarkEncodingInterfaceEncode(b *testing.B) {
	text := "推 jack86326: term.ptt.cc也有此現象，control+P 發文也有部分破圖    09/27 19:04"
	raw := []byte(text)
	enc := Big5UAO.NewEncoder()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_, _ = enc.Bytes(raw)
	}
}
