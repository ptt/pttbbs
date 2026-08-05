package main

import (
	"bytes"
	"encoding/binary"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func createMockDirFile(t *testing.T, dirFile string, articles []ArticleHeader) {
	var buf bytes.Buffer
	for _, art := range articles {
		var raw RawFileHeader
		copy(raw.Filename[:], art.Filename)
		copy(raw.Owner[:], art.Owner)
		copy(raw.Date[:], art.Date)
		b5Title := encodeToBig5(art.Title)
		copy(raw.Title[:], b5Title)
		raw.Filemode = art.Filemode
		binary.Write(&buf, binary.LittleEndian, &raw)
	}
	if err := os.WriteFile(dirFile, buf.Bytes(), 0644); err != nil {
		t.Fatalf("Failed to write mock .DIR file %s: %v", dirFile, err)
	}
}

func setupTestDir(t *testing.T) (string, func()) {
	tmpDir, err := os.MkdirTemp("", "viewdir_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}

	rootDirFile := filepath.Join(tmpDir, ".DIR")
	createMockDirFile(t, rootDirFile, []ArticleHeader{
		{Filename: "M.100", Owner: "SYSOP", Date: "01/01", Title: "Test Article 1"},
		{Filename: "M.101", Owner: "SYSOP", Date: "01/02", Title: "[建議] abc"},
		{Filename: "SubDir1", Owner: "SYSOP", Date: "01/03", Title: "Sub Directory 1"},
	})

	os.WriteFile(filepath.Join(tmpDir, "M.100"), []byte("Content 1\nLine 2"), 0644)
	os.WriteFile(filepath.Join(tmpDir, "M.101"), []byte("Content 2"), 0644)

	subDirPath := filepath.Join(tmpDir, "SubDir1")
	if err := os.MkdirAll(subDirPath, 0755); err != nil {
		t.Fatalf("Failed to create subDir: %v", err)
	}
	subDirFile := filepath.Join(subDirPath, ".DIR")
	createMockDirFile(t, subDirFile, []ArticleHeader{
		{Filename: "M.200", Owner: "SYSOP", Date: "01/04", Title: "Sub Article 200"},
	})
	os.WriteFile(filepath.Join(subDirPath, "M.200"), []byte("Sub Content 200"), 0644)

	cleanup := func() {
		os.RemoveAll(tmpDir)
	}
	return tmpDir, cleanup
}

func TestParseDirFile(t *testing.T) {
	tmpDir, cleanup := setupTestDir(t)
	defer cleanup()

	dirPath := filepath.Join(tmpDir, ".DIR")
	articles, err := parseDirFile(dirPath)
	if err != nil {
		t.Fatalf("Failed to parse .DIR: %v", err)
	}

	if len(articles) != 3 {
		t.Fatalf("Expected 3 articles, got %d", len(articles))
	}
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

func TestAllpostTitleParsing(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "allpost_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	boardsDir := filepath.Join(tmpDir, "boards")
	footballDir := filepath.Join(boardsDir, "F", "Football")
	allpostDir := filepath.Join(boardsDir, "A", "ALLPOST")
	if err := os.MkdirAll(footballDir, 0755); err != nil {
		t.Fatalf("Failed to create football dir: %v", err)
	}
	if err := os.MkdirAll(allpostDir, 0755); err != nil {
		t.Fatalf("Failed to create allpost dir: %v", err)
	}

	targetFile := filepath.Join(footballDir, "M.1785855146.A.CDF")
	os.WriteFile(targetFile, []byte("Football article content"), 0644)

	app := &AppState{
		baseDir: allpostDir,
		mode:    ModeDirView,
	}

	testCases := []struct {
		title    string
		filename string
		expected string
	}{
		{
			title:    "Fw: [新聞] 沙特足球告別撒錢模式，從…  (Football)",
			filename: "M.1785855146.A.CDF",
			expected: targetFile,
		},
		{
			title:    "Fw: [新聞] 沙特足球告別撒錢模式，從…  (Football   )",
			filename: "M.1785855146.A.CDF",
			expected: targetFile,
		},
		{
			title:    "Fw: [新聞] 沙特足球告別撒錢模式，從…  (  Football  )   ",
			filename: "M.1785855146.A.CDF",
			expected: targetFile,
		},
		{
			title:    "Fw: [新聞] 沙特足球 \x1b[1;30m(Football)\x1b[0m",
			filename: "M.1785855146.A.CDF",
			expected: targetFile,
		},
		{
			title:    "Fw: [新聞] (轉錄) 沙特足球 （Football） ",
			filename: "M.1785855146.A.CDF",
			expected: targetFile,
		},
	}

	for i, tc := range testCases {
		art := ArticleHeader{
			Title:    tc.title,
			Filename: tc.filename,
		}
		path := app.getArticlePath(art)
		if path != tc.expected {
			t.Errorf("Case %d: expected %s, got %s for title %q", i+1, tc.expected, path, tc.title)
		}
	}

	// Also test direct layout without category subdirectory: tmpDir/boards/ALLPOST
	directAllpostDir := filepath.Join(boardsDir, "ALLPOST")
	os.MkdirAll(directAllpostDir, 0755)
	appDirect := &AppState{
		baseDir: directAllpostDir,
		mode:    ModeDirView,
	}
	pathDirect := appDirect.getArticlePath(ArticleHeader{
		Title:    "Fw: [新聞] 沙特足球告別撒錢模式，從…  (Football   )",
		Filename: "M.1785855146.A.CDF",
	})
	if pathDirect != targetFile {
		t.Errorf("Direct layout: expected %s, got %s", targetFile, pathDirect)
	}
}

func TestSearchTitleOnly(t *testing.T) {
	tmpDir, cleanup := setupTestDir(t)
	defer cleanup()

	dirPath := filepath.Join(tmpDir, ".DIR")
	articles, err := parseDirFile(dirPath)
	if err != nil {
		t.Fatalf("Failed to parse .DIR: %v", err)
	}

	app := &AppState{
		baseDir:       tmpDir,
		dirPath:       dirPath,
		allArticles:   articles,
		articles:      articles,
		mode:          ModeDirView,
		outputEnc:     "utf8",
		selectedIndex: 0,
	}

	app.searchQuery = "abc"
	app.executeSearch()

	if len(app.articles) != 1 {
		t.Fatalf("Expected exactly 1 article matching 'abc' in title, got %d", len(app.articles))
	}
	if app.articles[0].Title != "[建議] abc" {
		t.Errorf("Expected title '[建議] abc', got '%s'", app.articles[0].Title)
	}
}

func TestSearchTriggerKeys(t *testing.T) {
	app := &AppState{
		mode: ModeDirView,
	}

	// Test '/' key triggers search mode
	app.handleInput([]byte{'/'})
	if app.mode != ModeSearchInput {
		t.Errorf("Expected '/' key to switch to ModeSearchInput, got %v", app.mode)
	}

	app.mode = ModeDirView
	// Test '?' key triggers search mode
	app.handleInput([]byte{'?'})
	if app.mode != ModeSearchInput {
		t.Errorf("Expected '?' key to switch to ModeSearchInput, got %v", app.mode)
	}
}

func TestRecursiveDirectoryView(t *testing.T) {
	tmpDir, cleanup := setupTestDir(t)
	defer cleanup()

	dirPath := filepath.Join(tmpDir, ".DIR")
	articles, err := parseDirFile(dirPath)
	if err != nil {
		t.Fatalf("Failed to parse .DIR: %v", err)
	}

	app := &AppState{
		baseDir:       tmpDir,
		dirPath:       dirPath,
		allArticles:   articles,
		articles:      articles,
		mode:          ModeDirView,
		outputEnc:     "utf8",
		selectedIndex: 2, // Select "SubDir1"
	}

	// 1. Enter SubDir1
	app.openArticle()

	if app.baseDir != filepath.Join(tmpDir, "SubDir1") {
		t.Errorf("Expected baseDir to be SubDir1, got %s", app.baseDir)
	}
	if len(app.dirStack) != 1 {
		t.Fatalf("Expected dirStack size 1, got %d", len(app.dirStack))
	}
	if len(app.articles) != 1 || app.articles[0].Title != "Sub Article 200" {
		t.Errorf("Expected Sub Article 200 in subDir, got %v", app.articles)
	}

	// 2. Press 'q' in subDir -> pop back to root dir
	quit := app.handleInput([]byte{'q'})
	if quit {
		t.Errorf("Expected 'q' in subDir to pop directory instead of quitting app")
	}
	if app.baseDir != tmpDir {
		t.Errorf("Expected baseDir to be restored to root %s, got %s", tmpDir, app.baseDir)
	}
	if len(app.dirStack) != 0 {
		t.Errorf("Expected dirStack size 0 after pop, got %d", len(app.dirStack))
	}
	if app.selectedIndex != 2 {
		t.Errorf("Expected restored selectedIndex to be 2, got %d", app.selectedIndex)
	}

	// 3. Enter SubDir1 again and test Left Arrow / 'h' navigation
	app.openArticle()
	if len(app.dirStack) != 1 {
		t.Fatalf("Expected dirStack size 1 on re-entering subDir")
	}
	app.onLeft() // Press Left Arrow
	if app.baseDir != tmpDir {
		t.Errorf("Expected onLeft in subDir to restore root dir, got %s", app.baseDir)
	}
}

func TestSanitizeDBCS(t *testing.T) {
	// Test 1: Normal text
	input1 := []byte("Hello World")
	out1 := sanitizeDBCS(input1)
	if string(out1) != "Hello World" {
		t.Errorf("Expected 'Hello World', got '%s'", string(out1))
	}

	// Test 2: Normal ANSI sequence
	input2 := []byte("Hello \x1b[1;31mRed\x1b[0m World")
	out2 := sanitizeDBCS(input2)
	if string(out2) != "Hello \x1b[1;31mRed\x1b[0m World" {
		t.Errorf("Expected preserved ANSI sequence, got '%s'", string(out2))
	}

	// Test 3: Broken DBCS (ANSI inserted between lead byte and tail byte)
	// \xa4 is a Big5 lead byte, \x40 is Big5 tail byte
	input3 := []byte("AB\xa4\x1b[1;31m\x40CD")
	out3 := sanitizeDBCS(input3)
	expected3 := []byte("AB\xa4\x40CD")
	if !bytes.Equal(out3, expected3) {
		t.Errorf("Expected broken DBCS ANSI to be stripped, got %v, expected %v", out3, expected3)
	}

	// Test 4: Multiple interrupting ANSI sequences
	input4 := []byte("\xa4\x1b[1m\x1b[31m\x40")
	out4 := sanitizeDBCS(input4)
	expected4 := []byte("\xa4\x40")
	if !bytes.Equal(out4, expected4) {
		t.Errorf("Expected multiple interrupting ANSI to be stripped, got %v, expected %v", out4, expected4)
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
	tmpDir, cleanup := setupTestDir(t)
	defer cleanup()

	dirPath := filepath.Join(tmpDir, ".DIR")
	articles, err := parseDirFile(dirPath)
	if err != nil {
		t.Fatalf("Failed to parse .DIR: %v", err)
	}

	// Restrict to file articles (M.100 and M.101)
	articles = articles[:2]

	app := &AppState{
		baseDir:       tmpDir,
		dirPath:       dirPath,
		allArticles:   articles,
		articles:      articles,
		mode:          ModeDirView,
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

	// 3. PageUp at top -> open prev article (selectedIndex 0)
	app.fileScrollLine = 0
	app.handleInput([]byte{0x1b, '[', '5', '~'}) // PageUp
	if app.selectedIndex != 0 {
		t.Errorf("Expected PageUp at top to switch to index 0, got %d", app.selectedIndex)
	}
}

func TestMultiplePathsExit(t *testing.T) {
	app := &AppState{
		mode: ModeDirView,
	}

	if !app.handleInput([]byte{'q'}) {
		t.Errorf("Expected 'q' in root dir view to return true for exiting path")
	}

	if !app.handleInput([]byte{0x1b, '[', 'D'}) {
		t.Errorf("Expected Left Arrow in root dir view to return true for exiting path")
	}

	if !app.handleInput([]byte{'h'}) {
		t.Errorf("Expected 'h' in root dir view to return true for exiting path")
	}
}

func TestTruncateFrontWidth(t *testing.T) {
	shortPath := "/sda4/bbs/boards"
	if res := truncateFrontWidth(shortPath, 64); res != shortPath {
		t.Errorf("Expected short path unchanged, got %q", res)
	}

	longPath := "/very/long/path/name/that/exceeds/the/maximum/allowed/header/title/length/limit/for/viewdir/testing/subfolder"
	res := truncateFrontWidth(longPath, 40)
	if stringWidth(res) > 40 {
		t.Errorf("Expected truncated width <= 40, got width %d (%q)", stringWidth(res), res)
	}
	if !strings.HasPrefix(res, "...") {
		t.Errorf("Expected prefix '...', got %q", res)
	}
	if !strings.HasSuffix(res, "testing/subfolder") {
		t.Errorf("Expected suffix 'testing/subfolder', got %q", res)
	}
}

func TestVirtualListPathResolution(t *testing.T) {
	tmpDir, cleanup := setupTestDir(t)
	defer cleanup()

	app := &AppState{
		baseDir: "路徑列表",
	}

	absTmpDir, _ := filepath.Abs(tmpDir)
	art := ArticleHeader{
		Filename: absTmpDir,
		Title:    absTmpDir,
		Filemode: 0x20,
	}

	resolvedPath := app.getArticlePath(art)
	if resolvedPath != absTmpDir {
		t.Errorf("Expected getArticlePath to return %q, got %q", absTmpDir, resolvedPath)
	}

	app.articles = []ArticleHeader{art}
	app.selectedIndex = 0
	app.openArticle()

	if app.baseDir != absTmpDir {
		t.Errorf("Expected baseDir after opening to be %q, got %q", absTmpDir, app.baseDir)
	}
	if app.dirPath != filepath.Join(absTmpDir, ".DIR") {
		t.Errorf("Expected dirPath after opening to be %q, got %q", filepath.Join(absTmpDir, ".DIR"), app.dirPath)
	}
}

func TestSymlinkLoadBalancingResolution(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "symlink_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	bbsHome := filepath.Join(tmpDir, "bbs")
	disk1 := filepath.Join(tmpDir, "disk1")
	disk2 := filepath.Join(tmpDir, "disk2")

	allpostRealDir := filepath.Join(disk1, "boards", "A", "ALLPOST")
	baseballRealDir := filepath.Join(disk2, "boards", "B", "Baseball")
	os.MkdirAll(allpostRealDir, 0755)
	os.MkdirAll(baseballRealDir, 0755)

	targetFile := filepath.Join(baseballRealDir, "M.100.A")
	os.WriteFile(targetFile, []byte("Baseball Content"), 0644)

	bbsBoardsDir := filepath.Join(bbsHome, "boards")
	os.MkdirAll(filepath.Join(bbsBoardsDir, "A"), 0755)
	os.MkdirAll(filepath.Join(bbsBoardsDir, "B"), 0755)

	os.Symlink(allpostRealDir, filepath.Join(bbsBoardsDir, "A", "ALLPOST"))
	os.Symlink(baseballRealDir, filepath.Join(bbsBoardsDir, "B", "Baseball"))

	os.Setenv("BBSHOME", bbsHome)

	appLogical := &AppState{
		baseDir: filepath.Join(bbsBoardsDir, "A", "ALLPOST"),
	}
	resolvedLogical := appLogical.getArticlePath(ArticleHeader{
		Title:    "Fw: [新聞] 棒球 (Baseball)",
		Filename: "M.100.A",
	})
	if resolvedLogical != targetFile && resolvedLogical != filepath.Join(bbsBoardsDir, "B", "Baseball", "M.100.A") {
		t.Errorf("Logical path: expected resolved target %s, got %s", targetFile, resolvedLogical)
	}

	appPhysical := &AppState{
		baseDir: allpostRealDir,
	}
	resolvedPhysical := appPhysical.getArticlePath(ArticleHeader{
		Title:    "Fw: [新聞] 棒球 (Baseball)",
		Filename: "M.100.A",
	})
	if resolvedPhysical != targetFile && resolvedPhysical != filepath.Join(bbsBoardsDir, "B", "Baseball", "M.100.A") {
		t.Errorf("Physical path fallback: expected resolved target %s, got %s", targetFile, resolvedPhysical)
	}
}
