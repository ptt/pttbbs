package daemon

type Request struct {
	Action string `json:"action"`           // status, update, fix, reset, rebuild, num, kick
	Target string `json:"target,omitempty"` // for kick: userid or pid
	Reason string `json:"reason,omitempty"` // for kick: reason description
}

type HotBoardInfo struct {
	BID     int    `json:"bid"`
	BrdName string `json:"brdname"`
	NUsers  int    `json:"nusers"`
}

type UTMPStatus struct {
	Now             string         `json:"now"`
	Uptime          string         `json:"uptime"`
	UptimeTimestamp int64          `json:"uptime_ts"`
	Number          int            `json:"number"`
	Busystate       int            `json:"busystate"`
	NeedUpdate      int            `json:"needupdate"`
	HotBoards       []HotBoardInfo `json:"hotboards,omitempty"`
}

type CleanDetail struct {
	Slot    int    `json:"slot"`
	PID     int    `json:"pid"`
	UID     int    `json:"uid"`
	UserID  string `json:"userid"`
	Reason  string `json:"reason"`
	IdleSec int    `json:"idle_sec"`
}

type FixResult struct {
	Scanned        int           `json:"scanned"`
	Active         int           `json:"active"`
	CleanedDead    int           `json:"cleaned_dead"`
	CleanedInvalid int           `json:"cleaned_invalid"`
	TotalCleaned   int           `json:"total_cleaned"`
	OnlineAfter    int           `json:"online_after"`
	TableFixed     bool          `json:"table_fixed"`
	Duration       string        `json:"duration"`
	Details        []CleanDetail `json:"details,omitempty"`
}

type Response struct {
	Success bool        `json:"success"`
	Message string      `json:"message,omitempty"`
	Status  *UTMPStatus `json:"status,omitempty"`
	Fix     *FixResult  `json:"fix,omitempty"`
	Number  *int        `json:"number,omitempty"`
	Error   string      `json:"error,omitempty"`
}
