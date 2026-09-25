package storage

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// GetHomeFile constructs the path home/<c>/<userid>/<filename> compatible with pttbbs
func GetHomeFile(bbsHome string, userid string, filename string) (string, error) {
	userid = strings.TrimSpace(userid)
	if len(userid) == 0 {
		return "", fmt.Errorf("empty userid")
	}
	exactChar := string(userid[0])
	exactDir := filepath.Join(bbsHome, "home", exactChar, userid)
	if _, err := os.Stat(exactDir); err != nil {
		lowerChar := strings.ToLower(exactChar)
		if lowerChar != exactChar {
			lowerDir := filepath.Join(bbsHome, "home", lowerChar, userid)
			if _, lerr := os.Stat(lowerDir); lerr == nil {
				return filepath.Join(lowerDir, filename), nil
			}
		}
	}
	return filepath.Join(exactDir, filename), nil
}

// GetBoardFile constructs the path boards/<c>/<brdname>/<filename> compatible with pttbbs setbfile
func GetBoardFile(bbsHome string, brdname string, filename string) (string, error) {
	brdname = strings.TrimSpace(brdname)
	if len(brdname) == 0 {
		return "", fmt.Errorf("empty brdname")
	}
	firstChar := string(brdname[0])
	return filepath.Join(bbsHome, "boards", firstChar, brdname, filename), nil
}

// extractASCIIUserID extracts the leading ASCII userid token (up to IDLEN=12 chars)
// from a raw line in Big5-encoded PTT list files (overrides, reject, alohaed, visable),
// ignoring any trailing Big5 description/comment without UTF-8 decoding.
func extractASCIIUserID(line []byte) string {
	i := 0
	for i < len(line) && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n') {
		i++
	}
	start := i
	for i < len(line) {
		b := line[i]
		if b == ' ' || b == '\t' || b == '\r' || b == '\n' || b >= 0x80 {
			break
		}
		i++
	}
	if i == start {
		return ""
	}
	token := line[start:i]
	if len(token) > 12 {
		return ""
	}
	for _, b := range token {
		if !((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9')) {
			return ""
		}
	}
	return string(token)
}

// LoadBoardVisableFile reads boards/<c>/<brdname>/visable without the legacy 256 (MAX_FRIEND) cap
func LoadBoardVisableFile(bbsHome string, brdname string) (targets []string, modTime time.Time, size int64, exists bool, err error) {
	filePath, err := GetBoardFile(bbsHome, brdname, "visable")
	if err != nil {
		return nil, time.Time{}, 0, false, err
	}

	info, err := os.Stat(filePath)
	if os.IsNotExist(err) {
		return nil, time.Time{}, 0, false, nil
	} else if err != nil {
		return nil, time.Time{}, 0, false, err
	}

	file, err := os.Open(filePath)
	if os.IsNotExist(err) {
		return nil, time.Time{}, 0, false, nil
	} else if err != nil {
		return nil, time.Time{}, 0, false, err
	}
	defer file.Close()

	seen := make(map[string]bool)
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		targetID := extractASCIIUserID(scanner.Bytes())
		if targetID == "" || strings.EqualFold(targetID, "guest") {
			continue
		}
		lower := strings.ToLower(targetID)
		if !seen[lower] {
			seen[lower] = true
			targets = append(targets, targetID)
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, time.Time{}, 0, false, err
	}
	return targets, info.ModTime(), info.Size(), true, nil
}

// LoadUserIDListFile reads home/<c>/<userid>/<filename> and returns deduplicated target userids
func LoadUserIDListFile(bbsHome string, subscriberUserID string, filename string) ([]string, error) {
	filePath, err := GetHomeFile(bbsHome, subscriberUserID, filename)
	if err != nil {
		return nil, err
	}

	file, err := os.Open(filePath)
	if os.IsNotExist(err) {
		return nil, nil
	} else if err != nil {
		return nil, err
	}
	defer file.Close()

	var targets []string
	seen := make(map[string]bool)

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		targetID := extractASCIIUserID(scanner.Bytes())
		if targetID == "" {
			continue
		}
		lower := strings.ToLower(targetID)
		if !seen[lower] {
			seen[lower] = true
			targets = append(targets, targetID)
		}
	}
	return targets, scanner.Err()
}

// LoadAlohaTargets reads home/<c>/<userid>/alohaed and returns list of targets watched by subscriber
func LoadAlohaTargets(bbsHome string, subscriberUserID string) ([]string, error) {
	return LoadUserIDListFile(bbsHome, subscriberUserID, "alohaed")
}

const (
	MaxFriends = 256
	MaxRejects = 32
)

// LoadFriendLists reads home/<c>/<userid>/overrides and home/<c>/<userid>/reject in file order.
// Callers must apply MaxFriends/MaxRejects after resolving IDs to uids, since
// (like legacy friend_load_real) only resolvable IDs count toward the limits.
func LoadFriendLists(bbsHome string, subscriberUserID string) (friends []string, rejects []string, err error) {
	friends, err = LoadUserIDListFile(bbsHome, subscriberUserID, "overrides")
	if err != nil {
		return nil, nil, err
	}
	rejects, err = LoadUserIDListFile(bbsHome, subscriberUserID, "reject")
	if err != nil {
		return friends, nil, err
	}
	return friends, rejects, nil
}


