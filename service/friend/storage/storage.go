package storage

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// GetHomeFile constructs the path home/<c>/<userid>/<filename> compatible with pttbbs
func GetHomeFile(bbsHome string, userid string, filename string) (string, error) {
	userid = strings.TrimSpace(userid)
	if len(userid) == 0 {
		return "", fmt.Errorf("empty userid")
	}
	firstChar := strings.ToLower(string(userid[0]))
	return filepath.Join(bbsHome, "home", firstChar, userid, filename), nil
}

// LoadAlohaTargets reads home/<c>/<userid>/alohaed and returns list of targets watched by subscriber
func LoadAlohaTargets(bbsHome string, subscriberUserID string) ([]string, error) {
	filePath, err := GetHomeFile(bbsHome, subscriberUserID, "alohaed")
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
		line := strings.TrimSpace(scanner.Text())
		if len(line) == 0 {
			continue
		}
		// Each line contains userid and optional remark
		fields := strings.Fields(line)
		if len(fields) > 0 {
			targetID := fields[0]
			if !seen[strings.ToLower(targetID)] {
				seen[strings.ToLower(targetID)] = true
				targets = append(targets, targetID)
			}
		}
	}
	return targets, scanner.Err()
}
