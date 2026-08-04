package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"unicode/utf8"

	"golang.org/x/term"
	"golang.org/x/text/encoding/traditionalchinese"
	"golang.org/x/text/transform"
)

// RawFileHeader represents fileheader_t structure in PTT (128 bytes)
type RawFileHeader struct {
	Filename  [28]byte
	Modified  uint32
	Pad       byte
	Recommend int8
	Owner     [14]byte
	Date      [6]byte
	Title     [65]byte
	Pad2      byte
	Multi     uint32
	Filemode  uint8
	Pad3      [3]byte
}

type ArticleHeader struct {
	Filename  string
	Modified  uint32
	Recommend int8
	Owner     string
	Date      string
	Title     string
	Filemode  uint8
}

type ViewMode int

const (
	ModeDirView ViewMode = iota
	ModeSearchInput
	ModeFileView
)

type AppState struct {
	baseDir   string
	dirPath   string
	outputEnc string // "utf8" or "big5"

	allArticles []ArticleHeader
	articles    []ArticleHeader
	mode        ViewMode

	// DirView State
	selectedIndex int
	scrollOffset  int

	// Search State
	inSearchMode   bool
	searchQuery    string
	savedSelectIdx int
	savedScrollOff int

	// FileView State
	currentArticle ArticleHeader
	fileLines      []string
	fileScrollLine int

	termWidth  int
	termHeight int

	rawOldState *term.State
}

func decodeBig5(b []byte) string {
	b = bytes.TrimRight(b, "\x00")
	r := transform.NewReader(bytes.NewReader(b), traditionalchinese.Big5.NewDecoder())
	decoded, err := io.ReadAll(r)
	if err != nil {
		return string(b)
	}
	return string(decoded)
}

func encodeToBig5(utf8Str string) []byte {
	encoder := traditionalchinese.Big5.NewEncoder()
	big5Bytes, err := encoder.Bytes([]byte(utf8Str))
	if err != nil {
		return []byte(utf8Str)
	}
	return big5Bytes
}

func parseDirFile(dirPath string) ([]ArticleHeader, error) {
	data, err := os.ReadFile(dirPath)
	if err != nil {
		return nil, err
	}

	var articles []ArticleHeader
	for i := 0; i+128 <= len(data); i += 128 {
		var raw RawFileHeader
		err := binary.Read(bytes.NewReader(data[i:i+128]), binary.LittleEndian, &raw)
		if err != nil {
			continue
		}

		filename := string(bytes.TrimRight(raw.Filename[:], "\x00"))
		if filename == "" {
			continue
		}

		owner := string(bytes.TrimRight(raw.Owner[:], "\x00"))
		date := string(bytes.TrimRight(raw.Date[:], "\x00"))
		title := decodeBig5(raw.Title[:])

		articles = append(articles, ArticleHeader{
			Filename:  filename,
			Modified:  raw.Modified,
			Recommend: raw.Recommend,
			Owner:     owner,
			Date:      date,
			Title:     title,
			Filemode:  raw.Filemode,
		})
	}
	return articles, nil
}

func formatRecommend(rec int8, isSelected bool) string {
	if rec == 0 {
		return "  "
	}

	if isSelected {
		if rec >= 100 {
			return "爆"
		}
		if rec > 0 {
			return fmt.Sprintf("%2d", rec)
		}
		if rec <= -100 {
			return "XX"
		}
		if rec <= -10 {
			return fmt.Sprintf("X%1d", -rec/10)
		}
		return fmt.Sprintf("%2d", rec)
	}

	if rec >= 100 {
		return "\x1b[1;35m爆\x1b[0m"
	}
	if rec > 9 {
		return fmt.Sprintf("\x1b[1;31m%2d\x1b[0m", rec)
	}
	if rec > 0 {
		return fmt.Sprintf("\x1b[1;33m%2d\x1b[0m", rec)
	}
	if rec <= -100 {
		return "\x1b[1;30mXX\x1b[0m"
	}
	if rec <= -10 {
		return fmt.Sprintf("\x1b[1;30mX%1d\x1b[0m", -rec/10)
	}
	return fmt.Sprintf("\x1b[1;30m%2d\x1b[0m", rec)
}

func runeWidth(r rune) int {
	if r >= 0x1100 && (
		r <= 0x115F ||
			r == 0x2329 || r == 0x232A ||
			(r >= 0x2E80 && r <= 0xA4CF && r != 0x303F) ||
			(r >= 0xAC00 && r <= 0xD7A3) ||
			(r >= 0xF900 && r <= 0xFAFF) ||
			(r >= 0xFE10 && r <= 0xFE19) ||
			(r >= 0xFE30 && r <= 0xFE6F) ||
			(r >= 0xFF00 && r <= 0xFF60) ||
			(r >= 0xFFE0 && r <= 0xFFE6) ||
			(r >= 0x20000 && r <= 0x2FFFD) ||
			(r >= 0x30000 && r <= 0x3FFFD)) {
		return 2
	}
	return 1
}

func stringWidth(s string) int {
	w := 0
	inEscape := false
	for _, r := range s {
		if r == '\x1b' {
			inEscape = true
			continue
		}
		if inEscape {
			if (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') {
				inEscape = false
			}
			continue
		}
		w += runeWidth(r)
	}
	return w
}

func padRightWidth(s string, width int) string {
	w := stringWidth(s)
	if w >= width {
		return truncateWidth(s, width)
	}
	return s + strings.Repeat(" ", width-w)
}

func truncateWidth(s string, maxWidth int) string {
	w := 0
	var res strings.Builder
	inEscape := false
	for _, r := range s {
		if r == '\x1b' {
			inEscape = true
			res.WriteRune(r)
			continue
		}
		if inEscape {
			res.WriteRune(r)
			if (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') {
				inEscape = false
			}
			continue
		}
		rw := runeWidth(r)
		if w+rw > maxWidth {
			break
		}
		w += rw
		res.WriteRune(r)
	}
	return res.String()
}

func (app *AppState) restoreTerminal() {
	if app.rawOldState != nil {
		term.Restore(int(os.Stdin.Fd()), app.rawOldState)
	}
	fmt.Print("\x1b[?25h\x1b[0m\x1b[2J\x1b[H") // restore cursor, reset colors, clear screen
}

func (app *AppState) updateTermSize() {
	w, h, err := term.GetSize(int(os.Stdout.Fd()))
	if err != nil || w <= 0 || h <= 0 {
		w, h = 80, 24
	}
	app.termWidth = w
	app.termHeight = h
}

func (app *AppState) render() {
	app.updateTermSize()

	var buf bytes.Buffer
	buf.WriteString("\x1b[H") // Cursor home

	switch app.mode {
	case ModeDirView, ModeSearchInput:
		app.renderDirView(&buf)
	case ModeFileView:
		app.renderFileView(&buf)
	}

	outputBytes := buf.Bytes()
	if app.outputEnc == "big5" {
		outputBytes = encodeToBig5(buf.String())
	}
	os.Stdout.Write(outputBytes)
}

func (app *AppState) renderDirView(buf *bytes.Buffer) {
	// Top Header Bar
	headerStr := fmt.Sprintf(" 看板 [%s] 文章列表  [共 %d 篇]", filepath.Base(app.baseDir), len(app.articles))
	if app.inSearchMode {
		headerStr = fmt.Sprintf(" 看板 [%s] 搜尋標題: \"%s\"  [共 %d 篇]", filepath.Base(app.baseDir), app.searchQuery, len(app.articles))
	}
	buf.WriteString(fmt.Sprintf("\x1b[1;37;44m%s\x1b[0m\r\n", padRightWidth(headerStr, app.termWidth)))

	// Column Header
	colHeader := "  編號   推文  日期   作者         標題"
	buf.WriteString(fmt.Sprintf("\x1b[1;33;40m%s\x1b[0m\r\n", padRightWidth(colHeader, app.termWidth)))

	// List area
	listHeight := app.termHeight - 3 // Header(1) + ColHeader(1) + Status(1)
	if listHeight < 1 {
		listHeight = 1
	}

	// Adjust scrollOffset to keep selectedIndex visible
	if app.selectedIndex < app.scrollOffset {
		app.scrollOffset = app.selectedIndex
	} else if app.selectedIndex >= app.scrollOffset+listHeight {
		app.scrollOffset = app.selectedIndex - listHeight + 1
	}

	for i := 0; i < listHeight; i++ {
		idx := app.scrollOffset + i
		buf.WriteString("\x1b[2K") // Clear line
		if idx >= len(app.articles) {
			buf.WriteString("~\r\n")
			continue
		}

		art := app.articles[idx]
		isSelected := (idx == app.selectedIndex)

		recStr := formatRecommend(art.Recommend, isSelected)
		ownerStr := art.Owner
		if stringWidth(ownerStr) > 12 {
			ownerStr = truncateWidth(ownerStr, 12)
		} else {
			ownerStr = padRightWidth(ownerStr, 12)
		}

		dateStr := padRightWidth(art.Date, 5)

		// Build output line
		prefix := "  "
		if isSelected {
			prefix = " >"
		}

		itemNo := fmt.Sprintf("%5d", idx+1)
		titleMaxLen := app.termWidth - 32
		if titleMaxLen < 10 {
			titleMaxLen = 10
		}
		titleStr := truncateWidth(art.Title, titleMaxLen)

		lineContent := fmt.Sprintf("%s%s %s %s %s %s", prefix, itemNo, recStr, dateStr, ownerStr, titleStr)

		if isSelected {
			buf.WriteString(fmt.Sprintf("\x1b[7m%s\x1b[0m\r\n", padRightWidth(lineContent, app.termWidth)))
		} else {
			buf.WriteString(fmt.Sprintf("%s\r\n", padRightWidth(lineContent, app.termWidth)))
		}
	}

	// Bottom Status Bar
	if app.mode == ModeSearchInput {
		promptStr := fmt.Sprintf(" 搜尋標題: %s█", app.searchQuery)
		buf.WriteString(fmt.Sprintf("\x1b[1;33;44m%s\x1b[0m", padRightWidth(promptStr, app.termWidth)))
	} else {
		statusStr := " [↑/↓: 選擇  Enter: 閱讀  /: 搜尋  PgUp/PgDn: 翻頁  q: 離開]"
		if app.inSearchMode {
			statusStr = fmt.Sprintf(" 搜尋結果 %d/%d  [Enter: 閱讀  ←/q: 結束搜尋  ↑/↓: 選擇]", app.selectedIndex+1, len(app.articles))
		} else if len(app.articles) > 0 {
			statusStr = fmt.Sprintf(" 文章 %d/%d  [↑/↓: 選擇  Enter: 閱讀  /: 搜尋  PgUp/PgDn: 翻頁  q: 離開]", app.selectedIndex+1, len(app.articles))
		}
		buf.WriteString(fmt.Sprintf("\x1b[1;37;44m%s\x1b[0m", padRightWidth(statusStr, app.termWidth)))
	}
}

func (app *AppState) renderFileView(buf *bytes.Buffer) {
	// Top Header Bar
	headerStr := fmt.Sprintf(" 文章閱讀: %s  [標題: %s]", app.currentArticle.Filename, app.currentArticle.Title)
	buf.WriteString(fmt.Sprintf("\x1b[1;37;44m%s\x1b[0m\r\n", padRightWidth(headerStr, app.termWidth)))

	// Content area
	contentHeight := app.termHeight - 2 // Header(1) + Status(1)
	if contentHeight < 1 {
		contentHeight = 1
	}

	totalLines := len(app.fileLines)
	if app.fileScrollLine < 0 {
		app.fileScrollLine = 0
	}
	maxScroll := totalLines - contentHeight
	if maxScroll < 0 {
		maxScroll = 0
	}
	if app.fileScrollLine > maxScroll {
		app.fileScrollLine = maxScroll
	}

	for i := 0; i < contentHeight; i++ {
		lineIdx := app.fileScrollLine + i
		buf.WriteString("\x1b[2K") // Clear line
		if lineIdx >= totalLines {
			buf.WriteString("~\r\n")
		} else {
			line := strings.ReplaceAll(app.fileLines[lineIdx], "\t", "    ")
			buf.WriteString(truncateWidth(line, app.termWidth))
			buf.WriteString("\r\n")
		}
	}

	// Status Bar
	percent := 100
	if totalLines > 0 {
		percent = ((app.fileScrollLine + contentHeight) * 100) / totalLines
		if percent > 100 {
			percent = 100
		}
	}
	statusStr := fmt.Sprintf(" [頁次: %d/%d (%d%%)]  (Left/q: 返回  ↑/↓/PgUp/PgDn/Space: 翻頁)", app.fileScrollLine+1, totalLines, percent)
	buf.WriteString(fmt.Sprintf("\x1b[1;37;44m%s\x1b[0m", padRightWidth(statusStr, app.termWidth)))
}

func (app *AppState) getArticlePath(art ArticleHeader) string {
	titleTrimmed := strings.TrimSpace(art.Title)
	var resolvedPath string

	if strings.HasSuffix(titleTrimmed, ")") {
		lastOpenParen := strings.LastIndex(titleTrimmed, "(")
		if lastOpenParen != -1 && lastOpenParen < len(titleTrimmed)-1 {
			boardName := strings.TrimSpace(titleTrimmed[lastOpenParen+1 : len(titleTrimmed)-1])
			if len(boardName) > 0 {
				parentDir := filepath.Dir(app.baseDir)
				boardsDir := filepath.Dir(parentDir)

				firstCharUpper := strings.ToUpper(string(boardName[0]))
				firstCharLower := strings.ToLower(string(boardName[0]))

				candidates := []string{
					filepath.Join(boardsDir, firstCharUpper, boardName, art.Filename),
					filepath.Join(boardsDir, firstCharLower, boardName, art.Filename),
					filepath.Join(boardsDir, firstCharUpper, strings.ToLower(boardName), art.Filename),
					filepath.Join(boardsDir, firstCharLower, strings.ToLower(boardName), art.Filename),
					filepath.Join(boardsDir, boardName, art.Filename),
				}

				for _, cand := range candidates {
					if _, err := os.Stat(cand); err == nil {
						resolvedPath = cand
						break
					}
				}
			}
		}
	}

	isAllPost := strings.EqualFold(filepath.Base(app.baseDir), "ALLPOST")
	if isAllPost && resolvedPath != "" {
		return resolvedPath
	}

	primaryPath := filepath.Join(app.baseDir, art.Filename)
	if _, err := os.Stat(primaryPath); err == nil {
		return primaryPath
	}

	if resolvedPath != "" {
		return resolvedPath
	}

	return primaryPath
}

func (app *AppState) openArticle() {
	if len(app.articles) == 0 || app.selectedIndex < 0 || app.selectedIndex >= len(app.articles) {
		return
	}

	art := app.articles[app.selectedIndex]
	filePath := app.getArticlePath(art)

	data, err := os.ReadFile(filePath)
	if err != nil {
		app.fileLines = []string{fmt.Sprintf("無法讀取檔案 %s: %v", filePath, err)}
	} else {
		decoded := decodeBig5(data)
		decoded = strings.ReplaceAll(decoded, "\r\n", "\n")
		decoded = strings.ReplaceAll(decoded, "\r", "\n")
		app.fileLines = strings.Split(decoded, "\n")
	}

	app.currentArticle = art
	app.fileScrollLine = 0
	app.mode = ModeFileView
}

func (app *AppState) executeSearch() {
	q := strings.TrimSpace(app.searchQuery)
	if q == "" {
		app.exitSearchMode()
		return
	}

	var filtered []ArticleHeader
	qLower := strings.ToLower(q)
	for _, art := range app.allArticles {
		if strings.Contains(strings.ToLower(art.Title), qLower) {
			filtered = append(filtered, art)
		}
	}

	if !app.inSearchMode {
		app.savedSelectIdx = app.selectedIndex
		app.savedScrollOff = app.scrollOffset
	}

	app.articles = filtered
	app.inSearchMode = true
	app.selectedIndex = 0
	if len(app.articles) > 0 {
		app.selectedIndex = len(app.articles) - 1
	}
	app.scrollOffset = 0
	app.mode = ModeDirView
}

func (app *AppState) exitSearchMode() {
	app.inSearchMode = false
	app.searchQuery = ""
	app.articles = app.allArticles
	app.selectedIndex = app.savedSelectIdx
	app.scrollOffset = app.savedScrollOff
	if app.selectedIndex >= len(app.articles) {
		app.selectedIndex = 0
	}
	app.mode = ModeDirView
}

func (app *AppState) isAtTop() bool {
	return app.fileScrollLine == 0
}

func (app *AppState) isAtBottom() bool {
	contentHeight := app.termHeight - 2
	if contentHeight < 1 {
		contentHeight = 1
	}
	maxScroll := len(app.fileLines) - contentHeight
	if maxScroll < 0 {
		maxScroll = 0
	}
	return app.fileScrollLine >= maxScroll
}

func (app *AppState) openPrevArticle() bool {
	if app.selectedIndex > 0 {
		app.selectedIndex--
		app.openArticle()
		return true
	}
	return false
}

func (app *AppState) openNextArticle() bool {
	if app.selectedIndex < len(app.articles)-1 {
		app.selectedIndex++
		app.openArticle()
		return true
	}
	return false
}

func detectDefaultEncoding() string {
	env := strings.ToLower(os.Getenv("LC_ALL") + " " + os.Getenv("LC_CTYPE") + " " + os.Getenv("LANG"))
	if strings.Contains(env, "big5") {
		return "big5"
	}
	return "utf8"
}

func main() {
	var encFlag string
	flag.StringVar(&encFlag, "encoding", "", "Output encoding (utf8 or big5)")
	flag.StringVar(&encFlag, "e", "", "Output encoding (utf8 or big5), shorthand")
	flag.Parse()

	inputPath := "."
	if flag.NArg() >= 1 {
		inputPath = flag.Arg(0)
	}

	outputEnc := strings.ToLower(strings.TrimSpace(encFlag))
	if outputEnc == "" {
		outputEnc = detectDefaultEncoding()
	}
	if outputEnc != "big5" && outputEnc != "utf8" {
		outputEnc = "utf8"
	}

	absPath, err := filepath.Abs(inputPath)
	if err == nil {
		if realPath, err := filepath.EvalSymlinks(absPath); err == nil {
			absPath = realPath
		}
		inputPath = absPath
	}

	fi, err := os.Stat(inputPath)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1)
	}

	var dirFile, baseDir string
	if fi.IsDir() {
		baseDir = inputPath
		dirFile = filepath.Join(inputPath, ".DIR")
	} else {
		dirFile = inputPath
		baseDir = filepath.Dir(inputPath)
	}

	articles, err := parseDirFile(dirFile)
	if err != nil {
		fmt.Printf("讀取 .DIR 失敗 (%s): %v\n", dirFile, err)
		os.Exit(1)
	}

	initialIndex := 0
	if len(articles) > 0 {
		initialIndex = len(articles) - 1
	}

	oldState, err := term.MakeRaw(int(os.Stdin.Fd()))
	if err != nil {
		fmt.Printf("Failed to enter raw terminal mode: %v\n", err)
		os.Exit(1)
	}

	app := &AppState{
		baseDir:       baseDir,
		dirPath:       dirFile,
		allArticles:   articles,
		articles:      articles,
		mode:          ModeDirView,
		outputEnc:     outputEnc,
		selectedIndex: initialIndex,
		scrollOffset:  0,
		rawOldState:   oldState,
	}

	// Setup signal handler for graceful exit
	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-c
		app.restoreTerminal()
		os.Exit(0)
	}()

	defer app.restoreTerminal()

	// Clear screen & hide cursor
	fmt.Print("\x1b[2J\x1b[?25l")

	app.render()

	buf := make([]byte, 128)
	for {
		n, err := os.Stdin.Read(buf)
		if err != nil || n == 0 {
			break
		}

		input := buf[:n]

		if app.handleInput(input) {
			break // Quit requested
		}

		app.render()
	}
}

func (app *AppState) handleInput(input []byte) bool {
	if app.mode == ModeSearchInput {
		return app.handleSearchInput(input)
	}

	if len(input) >= 3 && input[0] == 0x1b && input[1] == '[' {
		switch input[2] {
		case 'A': // Up Arrow (pmore.c KEY_UP)
			app.onUp()
			return false
		case 'B': // Down Arrow (pmore.c KEY_DOWN)
			app.onDown()
			return false
		case 'C': // Right Arrow (pmore.c KEY_RIGHT)
			app.onRight()
			return false
		case 'D': // Left Arrow (pmore.c KEY_LEFT)
			app.onLeft()
			return false
		case '5': // Page Up (\x1b[5~, pmore.c KEY_PGUP)
			app.onPageUp()
			return false
		case '6': // Page Down (\x1b[6~, pmore.c KEY_PGDN)
			app.onPageDown()
			return false
		case 'H', '1': // Home (pmore.c KEY_HOME)
			app.onHome()
			return false
		case 'F', '4': // End (pmore.c KEY_END)
			app.onEnd()
			return false
		}
	}

	for _, b := range input {
		switch b {
		case 'q', 'Q': // pmore.c 'q' -> exit
			if app.mode == ModeFileView {
				app.mode = ModeDirView
			} else if app.inSearchMode {
				app.exitSearchMode()
			} else {
				return true // Quit app
			}
		case '/': // pmore.c '/' -> search
			if app.mode == ModeDirView {
				app.mode = ModeSearchInput
				app.searchQuery = ""
				return false
			}
		case '\r', '\n': // pmore.c Enter -> down / next article
			app.onRight()
		case ' ': // pmore.c Space -> page down / next article
			app.onPageDown()
		case 'k': // pmore.c 'k' -> scroll up line
			if app.mode == ModeDirView {
				app.onUp()
			} else if app.mode == ModeFileView {
				if app.isAtTop() {
					app.openPrevArticle()
				} else {
					app.fileScrollLine--
				}
			}
		case 'j': // pmore.c 'j' -> scroll down line
			if app.mode == ModeDirView {
				app.onDown()
			} else if app.mode == ModeFileView {
				if app.isAtBottom() {
					app.openNextArticle()
				} else {
					app.fileScrollLine++
				}
			}
		case 'h': // pmore.c 'h' -> left
			app.onLeft()
		case 'l': // pmore.c 'l' -> right
			app.onRight()
		case 'b', 0x02: // pmore.c 'b' / Ctrl+B -> page up
			app.onPageUp()
		case 'f', 0x06: // pmore.c 'f' / Ctrl+F -> page down
			app.onPageDown()
		case '0', 'g': // pmore.c '0' / 'g' -> top
			app.onHome()
		case '$', 'G': // pmore.c '$' / 'G' -> bottom
			app.onEnd()
		}
	}
	return false
}

func (app *AppState) handleSearchInput(input []byte) bool {
	if len(input) == 1 && input[0] == 0x1b {
		app.mode = ModeDirView
		return false
	}

	var inputStr string
	if app.outputEnc == "big5" || !utf8.Valid(input) {
		inputStr = decodeBig5(input)
	} else {
		inputStr = string(input)
	}

	for _, r := range inputStr {
		if r == '\r' || r == '\n' {
			app.executeSearch()
			return false
		} else if r == 0x7f || r == '\b' {
			if len(app.searchQuery) > 0 {
				_, size := utf8.DecodeLastRuneInString(app.searchQuery)
				app.searchQuery = app.searchQuery[:len(app.searchQuery)-size]
			}
		} else if r == 0x1b { // ESC
			app.mode = ModeDirView
			return false
		} else if r == 0x03 { // Ctrl+C
			app.mode = ModeDirView
			return false
		} else if r >= 32 {
			app.searchQuery += string(r)
		}
	}
	return false
}

func (app *AppState) onUp() {
	if app.mode == ModeDirView {
		if app.selectedIndex > 0 {
			app.selectedIndex--
		}
	} else if app.mode == ModeFileView {
		if app.isAtTop() {
			app.openPrevArticle()
		} else {
			app.fileScrollLine--
		}
	}
}

func (app *AppState) onDown() {
	if app.mode == ModeDirView {
		if app.selectedIndex < len(app.articles)-1 {
			app.selectedIndex++
		}
	} else if app.mode == ModeFileView {
		if app.isAtBottom() {
			app.openNextArticle()
		} else {
			app.fileScrollLine++
		}
	}
}

func (app *AppState) onLeft() {
	if app.mode == ModeFileView {
		app.mode = ModeDirView
	} else if app.mode == ModeDirView && app.inSearchMode {
		app.exitSearchMode()
	}
}

func (app *AppState) onRight() {
	if app.mode == ModeDirView {
		app.openArticle()
	} else if app.mode == ModeFileView {
		if app.isAtBottom() {
			app.openNextArticle()
		} else {
			contentHeight := app.termHeight - 2
			if contentHeight < 1 {
				contentHeight = 1
			}
			app.fileScrollLine += contentHeight
		}
	}
}

func (app *AppState) onPageUp() {
	if app.mode == ModeDirView {
		pageSize := app.termHeight - 3
		if pageSize < 1 {
			pageSize = 1
		}
		app.selectedIndex -= pageSize
		if app.selectedIndex < 0 {
			app.selectedIndex = 0
		}
	} else if app.mode == ModeFileView {
		if app.isAtTop() {
			app.openPrevArticle()
		} else {
			contentHeight := app.termHeight - 2
			if contentHeight < 1 {
				contentHeight = 1
			}
			app.fileScrollLine -= contentHeight
			if app.fileScrollLine < 0 {
				app.fileScrollLine = 0
			}
		}
	}
}

func (app *AppState) onPageDown() {
	if app.mode == ModeDirView {
		pageSize := app.termHeight - 3
		if pageSize < 1 {
			pageSize = 1
		}
		app.selectedIndex += pageSize
		if app.selectedIndex >= len(app.articles) {
			app.selectedIndex = len(app.articles) - 1
		}
		if app.selectedIndex < 0 {
			app.selectedIndex = 0
		}
	} else if app.mode == ModeFileView {
		if app.isAtBottom() {
			app.openNextArticle()
		} else {
			contentHeight := app.termHeight - 2
			if contentHeight < 1 {
				contentHeight = 1
			}
			maxScroll := len(app.fileLines) - contentHeight
			if maxScroll < 0 {
				maxScroll = 0
			}
			app.fileScrollLine += contentHeight
			if app.fileScrollLine > maxScroll {
				app.fileScrollLine = maxScroll
			}
		}
	}
}

func (app *AppState) onHome() {
	if app.mode == ModeDirView {
		app.selectedIndex = 0
	} else if app.mode == ModeFileView {
		app.fileScrollLine = 0
	}
}

func (app *AppState) onEnd() {
	if app.mode == ModeDirView {
		if len(app.articles) > 0 {
			app.selectedIndex = len(app.articles) - 1
		}
	} else if app.mode == ModeFileView {
		contentHeight := app.termHeight - 2
		if contentHeight < 1 {
			contentHeight = 1
		}
		maxScroll := len(app.fileLines) - contentHeight
		if maxScroll < 0 {
			maxScroll = 0
		}
		app.fileScrollLine = maxScroll
	}
}
