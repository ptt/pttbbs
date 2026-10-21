package storage

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"pttbbs/stat/model"
)

type Storage struct {
	mu        sync.RWMutex
	byUser    map[string]*model.DailyStats
	filePath  string
	dirty     bool
	lastSaved time.Time
}

func NewStorage(filePath string) *Storage {
	return &Storage{
		byUser:   make(map[string]*model.DailyStats),
		filePath: filePath,
	}
}

func (s *Storage) normalize(userid string) string {
	return strings.ToLower(strings.TrimSpace(userid))
}

func (s *Storage) getEntryLocked(userid string) *model.DailyStats {
	if userid != "" {
		if entry, ok := s.byUser[userid]; ok {
			return entry
		}
	}
	return nil
}

func (s *Storage) getOrCreateLocked(userid string, today uint32) *model.DailyStats {
	entry := s.getEntryLocked(userid)
	if entry != nil {
		if entry.Date != today {
			entry.Date = today
			entry.PostCount = 0
			entry.MailCount = 0
			entry.SongCount = 0
			entry.LastSongTime = 0
			entry.UpdatedAt = time.Now().Unix()
			s.dirty = true
		}
		return entry
	}

	entry = &model.DailyStats{
		UserID:       userid,
		Date:         today,
		PostCount:    0,
		MailCount:    0,
		SongCount:    0,
		LastSongTime: 0,
		UpdatedAt:    time.Now().Unix(),
	}
	if userid != "" {
		s.byUser[userid] = entry
	}
	s.dirty = true
	return entry
}

func (s *Storage) Get(userid string) *model.DailyStats {
	userid = s.normalize(userid)
	today := model.TodayDate()

	s.mu.RLock()
	defer s.mu.RUnlock()

	entry := s.getEntryLocked(userid)
	if entry == nil || entry.Date != today {
		return &model.DailyStats{
			UserID:       userid,
			Date:         today,
			PostCount:    0,
			MailCount:    0,
			SongCount:    0,
			LastSongTime: 0,
			UpdatedAt:    time.Now().Unix(),
		}
	}

	copy := *entry
	return &copy
}

func (s *Storage) IncPost(userid string) (int32, error) {
	userid = s.normalize(userid)
	if userid == "" {
		return 0, fmt.Errorf("userid is required")
	}

	today := model.TodayDate()

	s.mu.Lock()
	defer s.mu.Unlock()

	entry := s.getOrCreateLocked(userid, today)
	entry.PostCount++
	entry.UpdatedAt = time.Now().Unix()
	s.dirty = true

	return entry.PostCount, nil
}

func (s *Storage) AddMail(userid string, count int) (int32, error) {
	userid = s.normalize(userid)
	if userid == "" {
		return 0, fmt.Errorf("userid is required")
	}
	if count <= 0 {
		count = 1
	}

	today := model.TodayDate()

	s.mu.Lock()
	defer s.mu.Unlock()

	entry := s.getOrCreateLocked(userid, today)
	entry.MailCount += int32(count)
	entry.UpdatedAt = time.Now().Unix()
	s.dirty = true

	return entry.MailCount, nil
}

func (s *Storage) IncSong(userid string) (int32, error) {
	userid = s.normalize(userid)
	if userid == "" {
		return 0, fmt.Errorf("userid is required")
	}

	today := model.TodayDate()
	now := time.Now().Unix()

	s.mu.Lock()
	defer s.mu.Unlock()

	entry := s.getOrCreateLocked(userid, today)
	entry.SongCount++
	entry.LastSongTime = now
	entry.UpdatedAt = now
	s.dirty = true

	return entry.SongCount, nil
}

func (s *Storage) Set(userid string, postCount, mailCount, songCount *int32) error {
	userid = s.normalize(userid)
	if userid == "" {
		return fmt.Errorf("userid is required")
	}

	today := model.TodayDate()

	s.mu.Lock()
	defer s.mu.Unlock()

	entry := s.getOrCreateLocked(userid, today)
	if postCount != nil {
		entry.PostCount = *postCount
	}
	if mailCount != nil {
		entry.MailCount = *mailCount
	}
	if songCount != nil {
		entry.SongCount = *songCount
	}
	entry.UpdatedAt = time.Now().Unix()
	s.dirty = true
	return nil
}

func (s *Storage) Prune(today uint32) int {
	s.mu.Lock()
	defer s.mu.Unlock()

	pruned := 0
	for k, entry := range s.byUser {
		if entry.Date < today {
			delete(s.byUser, k)
			pruned++
		}
	}
	if pruned > 0 {
		s.dirty = true
	}
	return pruned
}

func (s *Storage) CountTracked() int {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return len(s.byUser)
}

func (s *Storage) IsDirty() bool {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.dirty
}

func (s *Storage) LastSaved() time.Time {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.lastSaved
}

func (s *Storage) Save() (int, error) {
	if s.filePath == "" {
		return 0, nil
	}

	today := model.TodayDate()

	s.mu.Lock()
	entries := make([]*model.DailyStats, 0, len(s.byUser))
	for _, entry := range s.byUser {
		if entry.Date == today {
			entries = append(entries, entry)
		}
	}
	s.dirty = false
	s.lastSaved = time.Now()
	s.mu.Unlock()

	dir := filepath.Dir(s.filePath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return 0, err
	}

	data, err := json.MarshalIndent(entries, "", "  ")
	if err != nil {
		return 0, err
	}

	tmpFile := fmt.Sprintf("%s.tmp.%d", s.filePath, time.Now().UnixNano())
	if err := os.WriteFile(tmpFile, data, 0644); err != nil {
		return 0, err
	}

	if err := os.Rename(tmpFile, s.filePath); err != nil {
		_ = os.Remove(tmpFile)
		return 0, err
	}

	return len(entries), nil
}

func (s *Storage) Load() (int, error) {
	if s.filePath == "" {
		return 0, nil
	}

	data, err := os.ReadFile(s.filePath)
	if err != nil {
		if os.IsNotExist(err) {
			return 0, nil
		}
		return 0, err
	}

	var entries []*model.DailyStats
	if err := json.Unmarshal(data, &entries); err != nil {
		return 0, err
	}

	today := model.TodayDate()

	s.mu.Lock()
	defer s.mu.Unlock()

	loaded := 0
	for _, entry := range entries {
		// Only load entries matching today; yesterday's entries are naturally discarded.
		if entry.Date == today && entry.UserID != "" {
			s.byUser[strings.ToLower(entry.UserID)] = entry
			loaded++
		}
	}

	s.lastSaved = time.Now()
	s.dirty = false
	return loaded, nil
}
