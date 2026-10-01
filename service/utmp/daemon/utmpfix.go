package daemon

import (
	"log"
	"syscall"
	"time"

	"pttbbs/bbs"
)

// RunFix executes utmpfix on the shared memory to clean dead/invalid sessions and fix table links.
func RunFix(shm *bbs.SHMClient) FixResult {
	start := time.Now()
	res := FixResult{}

	if shm == nil {
		res.Duration = time.Since(start).String()
		return res
	}

	// 1. Fix broken table links
	tableFixed := shm.FixUtmpUserTable() != 0
	res.TableFixed = tableFixed

	// 2. Fetch session candidates
	candidates := shm.GetUtmpCandidates()
	res.Scanned = len(candidates)
	res.Active = len(candidates)

	var cleanedDetails []CleanDetail

	for _, cand := range candidates {
		// As long as the process is alive, never kick or purge its slot.
		if cand.PID > 0 {
			if err := syscall.Kill(cand.PID, 0); err == nil || err != syscall.ESRCH {
				continue
			}
		}

		cleanReason := "process error"
		isDeadProcess := true

		if !cand.IsValidUserID {
			cleanReason = "userid error"
			isDeadProcess = false
		} else if !cand.UserExists {
			cleanReason = "user not exist"
			isDeadProcess = false
		}

		shm.PurgeUtmpSlot(cand.Slot)

		cleanedDetails = append(cleanedDetails, CleanDetail{
			Slot:    cand.Slot,
			PID:     cand.PID,
			UID:     cand.UID,
			UserID:  cand.UserID,
			Reason:  cleanReason,
			IdleSec: cand.IdleSec,
		})

		if isDeadProcess {
			res.CleanedDead++
		} else {
			res.CleanedInvalid++
		}
	}

	res.TotalCleaned = len(cleanedDetails)
	res.Details = cleanedDetails

	// 3. Rebuild utmp_user table and update board stats if changed
	if res.TotalCleaned > 0 || tableFixed {
		shm.RebuildUtmpUser()
		shm.SetUtmpBusystate(0)
		shm.UtmpUpdate()
		shm.SetUtmpNeedUpdate(0)
	}

	res.OnlineAfter = shm.GetUtmpNumber()
	res.Duration = time.Since(start).String()

	if res.TotalCleaned > 0 || tableFixed {
		log.Printf("[utmp.svc] utmpfix completed in %s: cleaned %d sessions (dead=%d, invalid=%d), table_fixed=%v, online=%d",
			res.Duration, res.TotalCleaned, res.CleanedDead, res.CleanedInvalid, tableFixed, res.OnlineAfter)
	}

	return res
}
