package model

import (
	"fmt"
	"time"
)

// DailyStats represents a user's ephemeral daily statistics.
type DailyStats struct {
	UserID       string `json:"userid"`
	Date         uint32 `json:"date"`                     // YYYYMMDD (e.g., 20261021)
	PostCount    int32  `json:"post_count"`               // Today's credited posts count
	MailCount    int32  `json:"mail_count"`               // Today's sent mails count
	SongCount    int32  `json:"song_count"`               // Today's ordered songs count
	LastSongTime int64  `json:"last_song_time,omitempty"` // Timestamp of today's last ordered song
	UpdatedAt    int64  `json:"updated_at"`               // Unix timestamp
}

// Request is the JSON request payload for stat.svc.
type Request struct {
	Action    string `json:"action"` // "get", "inc", "add", "inc_post", "add_mail", "inc_song", "set", "status", "save", "prune"
	Key       string `json:"key,omitempty"` // "post", "mail", "song", "lastsong"
	UserID    string `json:"userid,omitempty"`
	Count     int    `json:"count,omitempty"`      // For add/add_mail
	PostCount *int32 `json:"post_count,omitempty"` // For set
	MailCount *int32 `json:"mail_count,omitempty"` // For set
	SongCount *int32 `json:"song_count,omitempty"` // For set
}

// Response is the JSON response payload from stat.svc.
type Response struct {
	Success      bool           `json:"success"`
	Value        int32          `json:"value"` // Generic returned value when key is requested
	Message      string         `json:"message,omitempty"`
	Error        string         `json:"error,omitempty"`
	Stats        *DailyStats    `json:"stats,omitempty"`
	PostCount    int32          `json:"post_count"`
	MailCount    int32          `json:"mail_count"`
	SongCount    int32          `json:"song_count"`
	LastSongTime int64          `json:"last_song_time,omitempty"`
	Status       *ServiceStatus `json:"status,omitempty"`
}

// ServiceStatus contains operational stats for stat.svc.
type ServiceStatus struct {
	Now          string `json:"now"`
	Date         uint32 `json:"date"`
	TrackedUsers int    `json:"tracked_users"`
	Uptime       string `json:"uptime"`
	LastSave     string `json:"last_save,omitempty"`
}

// TodayDate returns today's date formatted as YYYYMMDD (e.g. 20261021).
func TodayDate() uint32 {
	now := time.Now()
	return uint32(now.Year()*10000 + int(now.Month())*100 + now.Day())
}

// FormatDate formats a YYYYMMDD integer into "YYYY-MM-DD".
func FormatDate(d uint32) string {
	return fmt.Sprintf("%04d-%02d-%02d", d/10000, (d%10000)/100, d%100)
}
