package bbs

import "testing"

func TestBBSHome(t *testing.T) {
	home := BBSHome()
	if home == "" {
		t.Errorf("Expected non-empty BBSHome from C environment")
	}
}

func TestUTF8ToBig5(t *testing.T) {
	utf8Text := "測試水球"
	b, err := UTF8ToBig5(utf8Text)
	if err != nil {
		t.Fatalf("UTF8ToBig5 failed: %v", err)
	}
	if len(b) == 0 {
		t.Errorf("Expected non-empty Big5 bytes")
	}

	converted := Big5ToUTF8(b)
	if converted != utf8Text {
		t.Errorf("Big5ToUTF8 mismatch: expected %q, got %q", utf8Text, converted)
	}
}
