package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"unicode/utf8"

	"golang.org/x/term"
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

type dirFrame struct {
	baseDir        string
	dirPath        string
	allArticles    []ArticleHeader
	articles       []ArticleHeader
	selectedIndex  int
	scrollOffset   int
	inSearchMode   bool
	searchQuery    string
	savedSelectIdx int
	savedScrollOff int
}

type AppState struct {
	baseDir   string
	dirPath   string
	outputEnc string // "utf8" or "big5"

	dirStack    []dirFrame
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

func sanitizeDBCS(data []byte) []byte {
	out := make([]byte, 0, len(data))
	l := len(data)
	isInDBCS := false

	for i := 0; i < l; i++ {
		b := data[i]
		if b == 0x1b {
			j := i + 1
			if j < l && data[j] == '[' {
				j++
				for j < l && data[j] >= 0x20 && data[j] <= 0x3f {
					j++
				}
				if j < l && data[j] >= 0x40 && data[j] <= 0x7e {
					j++
				}
			} else if j < l && data[j] >= 0x40 && data[j] <= 0x5f {
				j++
			}

			if j > i+1 {
				if isInDBCS {
					// Interrupting ANSI escape sequence inside DBCS character. Ignore it.
					i = j - 1
					continue
				} else {
					// Normal ANSI escape sequence outside DBCS character. Keep it.
					out = append(out, data[i:j]...)
					i = j - 1
					continue
				}
			}
		}

		if isInDBCS {
			out = append(out, b)
			isInDBCS = false
		} else if b >= 0x80 {
			out = append(out, b)
			isInDBCS = true
		} else {
			out = append(out, b)
		}
	}
	return out
}

func decodeBig5(b []byte) string {
	b = bytes.TrimRight(b, "\x00")
	b = sanitizeDBCS(b)
	return decodeBig5Raw(b)
}

func encodeToBig5(utf8Str string) []byte {
	return encodeToBig5Raw(utf8Str)
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

		owner := decodeBig5(raw.Owner[:])
		date := decodeBig5(raw.Date[:])
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
	if (r >= 0x1100 && r <= 0x115F) ||
		r == 0x2329 || r == 0x232A ||
		(r >= 0x2000 && r <= 0x206F) || // General Punctuation (…, —, ※, ‘, ’, “, ”, etc.)
		(r >= 0x2100 && r <= 0x214F) || // Letterlike Symbols (℃, ℉, №, etc.)
		(r >= 0x2150 && r <= 0x218F) || // Number Forms (Ⅰ, Ⅱ, Ⅲ, etc.)
		(r >= 0x2190 && r <= 0x21FF) || // Arrows (←, ↑, →, ↓, etc.)
		(r >= 0x2200 && r <= 0x22FF) || // Mathematical Operators (√, ∞, ∕, etc.)
		(r >= 0x2300 && r <= 0x23FF) || // Misc Technical (①..⑩, etc.)
		(r >= 0x2460 && r <= 0x24FF) || // Enclosed Alphanumerics (①..⑳, etc.)
		(r >= 0x2500 && r <= 0x257F) || // Box Drawing (─, │, ┌, ┐, etc.)
		(r >= 0x2580 && r <= 0x259F) || // Block Elements (▀, ▄, █, etc.)
		(r >= 0x25A0 && r <= 0x25FF) || // Geometric Shapes (■, □, ▲, △, ▼, ▽, ◆, ◇, ★, ☆, etc.)
		(r >= 0x2600 && r <= 0x26FF) || // Misc Symbols (☀, ☁, ☂, ☎, ♀, ♂, etc.)
		(r >= 0x2E80 && r <= 0xA4CF && r != 0x303F) || // CJK Radicals, Symbols, CJK Unified Ideographs
		(r >= 0xAC00 && r <= 0xD7A3) || // Hangul Syllables
		(r >= 0xF900 && r <= 0xFAFF) || // CJK Compatibility Ideographs
		(r >= 0xFE10 && r <= 0xFE19) || // Vertical Forms
		(r >= 0xFE30 && r <= 0xFE6F) || // CJK Compatibility Forms
		(r >= 0xFF00 && r <= 0xFF60) || // Fullwidth Forms
		(r >= 0xFFE0 && r <= 0xFFE6) || // Fullwidth Symbol Variants
		(r >= 0x20000 && r <= 0x2FFFD) ||
		(r >= 0x30000 && r <= 0x3FFFD) {
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

func stripANSI(s string) string {
	var res strings.Builder
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
		res.WriteRune(r)
	}
	return res.String()
}

func resolveBoardArticlePath(boardsDir, boardName, filename string) string {
	if len(boardName) == 0 {
		return ""
	}

	firstCharUpper := strings.ToUpper(string(boardName[0]))
	firstCharLower := strings.ToLower(string(boardName[0]))

	candidates := []string{
		filepath.Join(boardsDir, firstCharUpper, boardName, filename),
		filepath.Join(boardsDir, firstCharLower, boardName, filename),
		filepath.Join(boardsDir, firstCharUpper, strings.ToLower(boardName), filename),
		filepath.Join(boardsDir, firstCharLower, strings.ToLower(boardName), filename),
		filepath.Join(boardsDir, boardName, filename),
	}

	for _, cand := range candidates {
		if _, err := os.Stat(cand); err == nil {
			return cand
		}
	}

	// Case-insensitive directory scan fallback
	dirCandidates := []string{
		filepath.Join(boardsDir, firstCharUpper),
		filepath.Join(boardsDir, firstCharLower),
		boardsDir,
	}

	for _, parentDir := range dirCandidates {
		entries, err := os.ReadDir(parentDir)
		if err != nil {
			continue
		}
		for _, entry := range entries {
			if entry.IsDir() && strings.EqualFold(entry.Name(), boardName) {
				cand := filepath.Join(parentDir, entry.Name(), filename)
				if _, err := os.Stat(cand); err == nil {
					return cand
				}
			}
		}
	}

	return ""
}

func (app *AppState) getArticlePath(art ArticleHeader) string {
	if filepath.IsAbs(art.Filename) {
		return art.Filename
	}

	cleanTitle := strings.TrimSpace(stripANSI(art.Title))
	var resolvedPath string

	lastClose := strings.LastIndexAny(cleanTitle, ")）")
	if lastClose != -1 {
		lastOpen := strings.LastIndexAny(cleanTitle[:lastClose], "(（")
		if lastOpen != -1 {
			_, openRuneSize := utf8.DecodeRuneInString(cleanTitle[lastOpen:])
			if lastOpen+openRuneSize < lastClose {
				boardName := strings.TrimSpace(cleanTitle[lastOpen+openRuneSize : lastClose])
				if len(boardName) > 0 {
					parentDir := filepath.Dir(app.baseDir)
					boardsDir := filepath.Dir(parentDir)
					resolvedPath = resolveBoardArticlePath(boardsDir, boardName, art.Filename)
					if resolvedPath == "" {
						resolvedPath = resolveBoardArticlePath(parentDir, boardName, art.Filename)
					}

					if resolvedPath == "" {
						var candidateBbsBoards []string
						if bbsHome := os.Getenv("BBSHOME"); bbsHome != "" {
							candidateBbsBoards = append(candidateBbsBoards, filepath.Join(bbsHome, "boards"))
						}
						candidateBbsBoards = append(candidateBbsBoards, "/home/bbs/boards")

						for _, candidateDir := range candidateBbsBoards {
							if candidateDir == boardsDir || candidateDir == parentDir {
								continue
							}
							if fi, err := os.Stat(candidateDir); err == nil && fi.IsDir() {
								resolvedPath = resolveBoardArticlePath(candidateDir, boardName, art.Filename)
								if resolvedPath != "" {
									break
								}
							}
						}
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

func (app *AppState) popDirectory() bool {
	if len(app.dirStack) == 0 {
		return false
	}
	n := len(app.dirStack) - 1
	frame := app.dirStack[n]
	app.dirStack = app.dirStack[:n]

	app.baseDir = frame.baseDir
	app.dirPath = frame.dirPath
	app.allArticles = frame.allArticles
	app.articles = frame.articles
	app.selectedIndex = frame.selectedIndex
	app.scrollOffset = frame.scrollOffset
	app.inSearchMode = frame.inSearchMode
	app.searchQuery = frame.searchQuery
	app.savedSelectIdx = frame.savedSelectIdx
	app.savedScrollOff = frame.savedScrollOff
	app.mode = ModeDirView
	return true
}

func (app *AppState) openArticle() {
	if len(app.articles) == 0 || app.selectedIndex < 0 || app.selectedIndex >= len(app.articles) {
		return
	}

	art := app.articles[app.selectedIndex]
	filePath := app.getArticlePath(art)

	fi, err := os.Stat(filePath)
	if err == nil {
		var subDirFile, subBaseDir string
		isDirIndex := false

		if fi.IsDir() {
			subBaseDir = filePath
			subDirFile = filepath.Join(filePath, ".DIR")
			isDirIndex = true
		} else if strings.HasSuffix(filePath, ".DIR") || filepath.Base(filePath) == ".DIR" {
			subBaseDir = filepath.Dir(filePath)
			subDirFile = filePath
			isDirIndex = true
		}

		if isDirIndex {
			subArticles, err := parseDirFile(subDirFile)
			if err == nil {
				frame := dirFrame{
					baseDir:        app.baseDir,
					dirPath:        app.dirPath,
					allArticles:    app.allArticles,
					articles:       app.articles,
					selectedIndex:  app.selectedIndex,
					scrollOffset:   app.scrollOffset,
					inSearchMode:   app.inSearchMode,
					searchQuery:    app.searchQuery,
					savedSelectIdx: app.savedSelectIdx,
					savedScrollOff: app.savedScrollOff,
				}
				app.dirStack = append(app.dirStack, frame)

				app.baseDir = subBaseDir
				app.dirPath = subDirFile
				app.allArticles = subArticles
				app.articles = subArticles
				app.selectedIndex = 0
				if len(subArticles) > 0 {
					app.selectedIndex = len(subArticles) - 1
				}
				app.scrollOffset = 0
				app.inSearchMode = false
				app.searchQuery = ""
				app.mode = ModeDirView
				return
			}
		}
	}

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

func truncateFrontWidth(s string, maxWidth int) string {
	w := stringWidth(s)
	if w <= maxWidth {
		return s
	}

	prefix := "..."
	prefixW := stringWidth(prefix)
	targetWidth := maxWidth - prefixW
	if targetWidth < 1 {
		targetWidth = 1
	}

	runes := []rune(s)
	suffixWidth := 0
	cutIdx := len(runes)
	for i := len(runes) - 1; i >= 0; i-- {
		rw := runeWidth(runes[i])
		if suffixWidth+rw > targetWidth {
			break
		}
		suffixWidth += rw
		cutIdx = i
	}

	return prefix + string(runes[cutIdx:])
}

func main() {
	var encFlag string
	flag.StringVar(&encFlag, "encoding", "", "Output encoding (utf8 or big5)")
	flag.StringVar(&encFlag, "e", "", "Output encoding (utf8 or big5), shorthand")
	flag.Parse()

	paths := flag.Args()
	if len(paths) == 0 {
		paths = []string{"."}
	}

	outputEnc := strings.ToLower(strings.TrimSpace(encFlag))
	if outputEnc == "" {
		outputEnc = detectDefaultEncoding()
	}
	if outputEnc != "big5" && outputEnc != "utf8" {
		outputEnc = "utf8"
	}

	oldState, err := term.MakeRaw(int(os.Stdin.Fd()))
	if err != nil {
		fmt.Printf("Failed to enter raw terminal mode: %v\n", err)
		os.Exit(1)
	}

	var articles []ArticleHeader
	var baseDir, dirFile string

	if len(paths) == 1 {
		inputPath := paths[0]
		absPath, err := filepath.Abs(inputPath)
		if err == nil {
			inputPath = absPath
		}

		fi, err := os.Stat(inputPath)
		if err != nil {
			term.Restore(int(os.Stdin.Fd()), oldState)
			fmt.Printf("Error: %v\n", err)
			os.Exit(1)
		}

		if fi.IsDir() {
			baseDir = inputPath
			dirFile = filepath.Join(inputPath, ".DIR")
		} else {
			dirFile = inputPath
			baseDir = filepath.Dir(inputPath)
		}

		arts, err := parseDirFile(dirFile)
		if err != nil {
			term.Restore(int(os.Stdin.Fd()), oldState)
			fmt.Printf("讀取 .DIR 失敗 (%s): %v\n", dirFile, err)
			os.Exit(1)
		}
		articles = arts
	} else {
		baseDir = "路徑列表"
		dirFile = "路徑列表"
		for _, rawPath := range paths {
			absPath, err := filepath.Abs(rawPath)
			if err != nil {
				absPath = rawPath
			}

			displayTitle := truncateFrontWidth(rawPath, 64)
			articles = append(articles, ArticleHeader{
				Filename: absPath,
				Owner:    "DIR",
				Date:     "",
				Title:    displayTitle,
				Filemode: 0x20, // Directory bit
			})
		}
	}

	initialIndex := 0
	if len(articles) > 0 {
		initialIndex = len(articles) - 1
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
			return app.onLeft()
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
			} else if app.popDirectory() {
				// Popped sub-directory
			} else {
				return true // Quit app
			}
		case '/', '?': // pmore.c '/' or '?' -> search
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
			if app.onLeft() {
				return true
			}
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
		switch r {
		case '\r', '\n':
			app.executeSearch()
		case 0x7f, 0x08: // Backspace
			if len(app.searchQuery) > 0 {
				runes := []rune(app.searchQuery)
				app.searchQuery = string(runes[:len(runes)-1])
			}
		case 0x1b, 0x03: // ESC / Ctrl+C
			app.mode = ModeDirView
			return false
		default:
			if r >= 32 {
				app.searchQuery += string(r)
			}
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

func (app *AppState) onLeft() bool {
	if app.mode == ModeFileView {
		app.mode = ModeDirView
		return false
	} else if app.mode == ModeDirView {
		if app.inSearchMode {
			app.exitSearchMode()
			return false
		} else if app.popDirectory() {
			return false
		} else {
			return true
		}
	}
	return false
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
