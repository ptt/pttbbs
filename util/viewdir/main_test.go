package main

import (
	"os"
	"testing"
)

func TestParseDirFile(t *testing.T) {
	dirPath := "/home/bbs/boards/S/SYSOP/.DIR"
	articles, err := parseDirFile(dirPath)
	if err != nil {
		t.Fatalf("Failed to parse .DIR: %v", err)
	}

	if len(articles) == 0 {
		t.Fatalf("Expected articles, got 0")
	}

	t.Logf("Parsed %d articles", len(articles))
}

func TestDetectEncoding(t *testing.T) {
	os.Setenv("LANG", "zh_TW.Big5")
	if enc := detectDefaultEncoding(); enc != "big5" {
		t.Errorf("Expected big5 for zh_TW.Big5, got %s", enc)
	}

	os.Setenv("LANG", "en_US.UTF-8")
	if enc := detectDefaultEncoding(); enc != "utf8" {
		t.Errorf("Expected utf8 for en_US.UTF-8, got %s", enc)
	}
}

func TestSearchTitleOnly(t *testing.T) {
	dirPath := "/home/bbs/boards/S/SYSOP/.DIR"
	articles, err := parseDirFile(dirPath)
	if err != nil {
		t.Fatalf("Failed to parse .DIR: %v", err)
	}

	app := &AppState{
		baseDir:       "/home/bbs/boards/S/SYSOP",
		dirPath:       dirPath,
		allArticles:   articles,
		articles:      articles,
		mode:          ModeDirView,
		outputEnc:     "utf8",
		selectedIndex: 0,
	}

	app.searchQuery = "a"
	app.executeSearch()

	if len(app.articles) != 1 {
		t.Fatalf("Expected exactly 1 article matching 'a' in title, got %d", len(app.articles))
	}
	if app.articles[0].Title != "[建議] abc" {
		t.Errorf("Expected title '[建議] abc', got '%s'", app.articles[0].Title)
	}
}

func TestAllpostResolution(t *testing.T) {
	dirPath := "/home/bbs/boards/A/ALLPOST/.DIR"
	articles, err := parseDirFile(dirPath)
	if err != nil {
		t.Fatalf("Failed to parse ALLPOST .DIR: %v", err)
	}

	if len(articles) == 0 {
		t.Fatalf("Expected articles in ALLPOST, got 0")
	}

	app := &AppState{
		baseDir:       "/home/bbs/boards/A/ALLPOST",
		dirPath:       dirPath,
		allArticles:   articles,
		articles:      articles,
		mode:          ModeDirView,
		outputEnc:     "utf8",
		selectedIndex: 0,
	}

	for i, art := range articles {
		resolvedPath := app.getArticlePath(art)
		t.Logf("[%d] Title: %s -> Resolved: %s", i+1, art.Title, resolvedPath)
		if resolvedPath == "/home/bbs/boards/A/ALLPOST/"+art.Filename {
			t.Fatalf("Expected resolved path to point to board dir, got ALLPOST path: %s", resolvedPath)
		}
		if _, err := os.Stat(resolvedPath); err != nil {
			t.Fatalf("Resolved path does not exist: %s (%v)", resolvedPath, err)
		}
	}
}

func TestFileViewEndKey(t *testing.T) {
	lines := make([]string, 100)
	for i := 0; i < 100; i++ {
		lines[i] = "Line"
	}

	app := &AppState{
		mode:           ModeFileView,
		fileLines:      lines,
		fileScrollLine: 0,
		termWidth:      80,
		termHeight:     24,
	}

	app.onEnd()

	expectedScroll := 100 - 22
	if app.fileScrollLine != expectedScroll {
		t.Errorf("Expected fileScrollLine to be %d on End key, got %d", expectedScroll, app.fileScrollLine)
	}
}

func TestPmoreNavigationKeys(t *testing.T) {
	dirPath := "/home/bbs/boards/S/SYSOP/.DIR"
	articles, err := parseDirFile(dirPath)
	if err != nil {
		t.Fatalf("Failed to parse .DIR: %v", err)
	}

	app := &AppState{
		baseDir:       "/home/bbs/boards/S/SYSOP",
		dirPath:       dirPath,
		allArticles:   articles,
		articles:      articles,
		mode:          ModeFileView,
		selectedIndex: 1,
		termWidth:     80,
		termHeight:    24,
	}
	app.openArticle()

	// 1. Up arrow at top -> open prev article (selectedIndex 0)
	app.fileScrollLine = 0
	app.handleInput([]byte{0x1b, '[', 'A'}) // Up Arrow
	if app.selectedIndex != 0 {
		t.Errorf("Expected Up arrow at top to switch to index 0, got %d", app.selectedIndex)
	}

	// 2. Down arrow at bottom -> open next article (selectedIndex 1)
	app.onEnd()
	app.handleInput([]byte{0x1b, '[', 'B'}) // Down Arrow
	if app.selectedIndex != 1 {
		t.Errorf("Expected Down arrow at bottom to switch to index 1, got %d", app.selectedIndex)
	}

	// 3. Space at bottom -> open next article (selectedIndex 2)
	app.onEnd()
	app.handleInput([]byte{' '})
	if app.selectedIndex != 2 {
		t.Errorf("Expected Space at bottom to switch to index 2, got %d", app.selectedIndex)
	}

	// 4. PageUp at top -> open prev article (selectedIndex 1)
	app.fileScrollLine = 0
	app.handleInput([]byte{0x1b, '[', '5', '~'}) // PageUp
	if app.selectedIndex != 1 {
		t.Errorf("Expected PageUp at top to switch to index 1, got %d", app.selectedIndex)
	}
}
