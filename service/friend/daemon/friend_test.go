package daemon

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"pttbbs/friend/storage"
)

func writeUserListFile(t *testing.T, bbsHome, userID, filename, content string) {
	t.Helper()
	p, err := storage.GetHomeFile(bbsHome, userID, filename)
	if err != nil {
		t.Fatalf("GetHomeFile failed: %v", err)
	}
	if err := os.MkdirAll(filepath.Dir(p), 0755); err != nil {
		t.Fatalf("MkdirAll failed: %v", err)
	}
	if err := os.WriteFile(p, []byte(content), 0644); err != nil {
		t.Fatalf("WriteFile failed: %v", err)
	}
}

func TestFriendOnlineGraphAndSlotEviction(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "aloha_friend_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	// Alice friends Bob and rejects Eve
	writeUserListFile(t, tempDir, "alice", "overrides", "bob best friend\n")
	writeUserListFile(t, tempDir, "alice", "reject", "eve spammer\n")

	// Bob friends Alice
	writeUserListFile(t, tempDir, "bob", "overrides", "alice\n")

	svc, err := NewService("")
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	svc.bbsHome = tempDir

	// 1. Alice (uid=10, pid=1001, sid=5) syncs friends while Bob & Eve are offline
	resp := svc.HandleFriendSync("alice", 10, 1001, 5)
	if !resp.Success {
		t.Fatalf("Alice friend_sync failed: %v", resp.Message)
	}
	if got := svc.GetSessionFriendsOnline(5); len(got) != 0 {
		t.Fatalf("Expected 0 online friends for Alice while peers offline, got %v", got)
	}

	// 2. Bob (uid=20, pid=1002, sid=12) syncs friends -> mutual friend (IFH | HFM) with Alice
	resp = svc.HandleFriendSync("bob", 20, 1002, 12)
	if !resp.Success {
		t.Fatalf("Bob friend_sync failed: %v", resp.Message)
	}

	aliceFriends := svc.GetSessionFriendsOnline(5)
	if len(aliceFriends) != 1 {
		t.Fatalf("Expected 1 online friend for Alice, got %d", len(aliceFriends))
	}
	stat, uidTag, slot := UnpackFriendOnline(aliceFriends[0])
	if stat != (FriendBitIFH|FriendBitHFM) || slot != 12 || uidTag != (20%63+1) {
		t.Fatalf("Unexpected Alice friend_online[0]: stat=%d slot=%d uidTag=%d", stat, slot, uidTag)
	}

	bobFriends := svc.GetSessionFriendsOnline(12)
	if len(bobFriends) != 1 {
		t.Fatalf("Expected 1 online friend for Bob, got %d", len(bobFriends))
	}
	stat, uidTag, slot = UnpackFriendOnline(bobFriends[0])
	if stat != (FriendBitIFH|FriendBitHFM) || slot != 5 || uidTag != (10%63+1) {
		t.Fatalf("Unexpected Bob friend_online[0]: stat=%d slot=%d uidTag=%d", stat, slot, uidTag)
	}

	// 3. Eve (uid=30, pid=1003, sid=25) logs in -> Alice has IRH(25), Eve has HRM(5)
	resp = svc.HandleFriendSync("eve", 30, 1003, 25)
	if !resp.Success {
		t.Fatalf("Eve friend_sync failed: %v", resp.Message)
	}

	aliceFriends = svc.GetSessionFriendsOnline(5)
	if len(aliceFriends) != 2 {
		t.Fatalf("Expected 2 online entries for Alice (Bob + Eve), got %d", len(aliceFriends))
	}
	stat, uidTag, slot = UnpackFriendOnline(aliceFriends[1])
	if stat != FriendBitIRH || slot != 25 || uidTag != (30%63+1) {
		t.Fatalf("Unexpected Alice friend_online[1]: stat=%d slot=%d uidTag=%d", stat, slot, uidTag)
	}

	eveFriends := svc.GetSessionFriendsOnline(25)
	if len(eveFriends) != 1 {
		t.Fatalf("Expected 1 online entry for Eve (HRM from Alice), got %d", len(eveFriends))
	}
	stat, uidTag, slot = UnpackFriendOnline(eveFriends[0])
	if stat != FriendBitHRM || slot != 5 || uidTag != (10%63+1) {
		t.Fatalf("Unexpected Eve friend_online[0]: stat=%d slot=%d uidTag=%d", stat, slot, uidTag)
	}

	// 4. Query Alice while online -> online=true, session_count=1, friends_status="loaded"
	qAlice := svc.HandleQueryUser("alice")
	if !qAlice.Success {
		t.Fatalf("HandleQueryUser(alice) failed: %v", qAlice.Message)
	}
	reportAlice, ok := qAlice.Data.(UserQueryReport)
	if !ok {
		t.Fatalf("Expected UserQueryReport, got %T", qAlice.Data)
	}
	if !reportAlice.Online || reportAlice.SessionCount != 1 || reportAlice.FriendsStatus != "loaded" {
		t.Fatalf("Unexpected Alice query report: %+v", reportAlice)
	}
	if len(reportAlice.MyFriends) != 1 || len(reportAlice.MyRejects) != 1 || len(reportAlice.Sessions[0].OnlineFriends) != 2 {
		t.Fatalf("Unexpected Alice friends/sessions in query report: %+v", reportAlice)
	}

	// 5. Simulate Bob (pid=1002, sid=12) crashing with kill -9 and Charlie (uid=40, pid=2002) reusing sid=12
	resp = svc.HandleFriendSync("charlie", 40, 2002, 12)
	if !resp.Success {
		t.Fatalf("Charlie friend_sync on reused sid=12 failed: %v", resp.Message)
	}

	// Alice's friend_online should automatically evict Bob (sid=12) and only keep Eve (sid=25)
	aliceFriends = svc.GetSessionFriendsOnline(5)
	if len(aliceFriends) != 1 {
		t.Fatalf("Expected Bob to be evicted from Alice's friend_online after sid=12 reuse, got %d entries", len(aliceFriends))
	}
	stat, _, slot = UnpackFriendOnline(aliceFriends[0])
	if slot != 25 || stat != FriendBitIRH {
		t.Fatalf("Expected remaining entry for Alice to be Eve (sid=25, IRH), got slot=%d stat=%d", slot, stat)
	}

	// 6. Eve logs out -> Alice's friend_online becomes empty, and querying Eve shows online=false, friends_status="not_loaded (user offline)"
	resp = svc.HandleUserLogout("eve", 1003)
	if !resp.Success {
		t.Fatalf("Eve logout failed: %v", resp.Message)
	}
	if got := svc.GetSessionFriendsOnline(5); len(got) != 0 {
		t.Fatalf("Expected 0 online entries for Alice after Eve logout, got %v", got)
	}
	qEve := svc.HandleQueryUser("eve")
	reportEve, ok := qEve.Data.(UserQueryReport)
	if !ok || reportEve.Online || reportEve.SessionCount != 0 || reportEve.FriendsStatus != "not_loaded (user offline)" {
		t.Fatalf("Expected Eve query report to show offline and not_loaded, got: %+v", reportEve)
	}
}

func TestHBFLHiddenBoardFriends(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "pttbbs_hbfl_test_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	secretDir := filepath.Join(tempDir, "boards", "S", "Secret")
	vipDir := filepath.Join(tempDir, "boards", "V", "VIP")
	if err := os.MkdirAll(secretDir, 0755); err != nil {
		t.Fatalf("Failed to create Secret board dir: %v", err)
	}
	if err := os.MkdirAll(vipDir, 0755); err != nil {
		t.Fatalf("Failed to create VIP board dir: %v", err)
	}

	// Write 600 users to boards/S/Secret/visable (exceeding legacy MAX_FRIEND=256 cap)
	var secretLines []string
	secretLines = append(secretLines, "guest should_be_ignored")
	for i := 1; i <= 600; i++ {
		secretLines = append(secretLines, fmt.Sprintf("member%03d comment %d", i, i))
	}
	secretVisablePath := filepath.Join(secretDir, "visable")
	if err := os.WriteFile(secretVisablePath, []byte(strings.Join(secretLines, "\n")+"\n"), 0644); err != nil {
		t.Fatalf("Failed to write Secret visable: %v", err)
	}

	// Write member500 and member600 to boards/V/VIP/visable
	vipVisablePath := filepath.Join(vipDir, "visable")
	if err := os.WriteFile(vipVisablePath, []byte("member500\nmember600\n"), 0644); err != nil {
		t.Fatalf("Failed to write VIP visable: %v", err)
	}

	svc, err := NewService(tempDir)
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}

	// Register boards with explicit 1-indexed BIDs
	if cnt := svc.RegisterBoard(15, "Secret", 0x8); cnt != 600 {
		t.Fatalf("Expected 600 members in Secret (bid=15), got %d", cnt)
	}
	if cnt := svc.RegisterBoard(88, "VIP", 0x8); cnt != 2 {
		t.Fatalf("Expected 2 members in VIP (bid=88), got %d", cnt)
	}

	// Verify member500 (well past index 256!) has access to both bid 15 and bid 88 via hbfl_user
	uResp := svc.HandleHBFLUser(0, "member500")
	if !uResp.Success || len(uResp.BIDs) != 2 || uResp.BIDs[0] != 15 || uResp.BIDs[1] != 88 {
		t.Fatalf("Expected member500 to have access to [15, 88], got %+v", uResp)
	}
	genBefore := uResp.Gen

	// Verify member001 only has access to bid 15
	uResp1 := svc.HandleHBFLUser(0, "member001")
	if !uResp1.Success || len(uResp1.BIDs) != 1 || uResp1.BIDs[0] != 15 {
		t.Fatalf("Expected member001 to have access to [15], got %+v", uResp1)
	}

	// Verify hbfl_check for member600 on Secret (true) and member001 on VIP (false)
	chk600 := svc.HandleHBFLCheck(15, "", 0, "member600")
	if !chk600.Success || chk600.Allowed == nil || !*chk600.Allowed {
		t.Fatalf("Expected member600 allowed on bid=15, got %+v", chk600)
	}
	chk001 := svc.HandleHBFLCheck(88, "", 0, "member001")
	if !chk001.Success || chk001.Allowed == nil || *chk001.Allowed {
		t.Fatalf("Expected member001 denied on bid=88, got %+v", chk001)
	}

	// Modify boards/V/VIP/visable to remove member500 and add member001 (with updated mtime/size)
	if err := os.WriteFile(vipVisablePath, []byte("member001\nmember600\nnewmember\n"), 0644); err != nil {
		t.Fatalf("Failed to update VIP visable: %v", err)
	}
	futureTime := time.Now().Add(5 * time.Second)
	_ = os.Chtimes(vipVisablePath, futureTime, futureTime)

	// Query member500 again: mtime change should automatically update reverse index and bump generation
	uRespAfter := svc.HandleHBFLUser(0, "member500")
	if !uRespAfter.Success || len(uRespAfter.BIDs) != 1 || uRespAfter.BIDs[0] != 15 {
		t.Fatalf("Expected member500 to only have [15] after VIP update, got %+v", uRespAfter)
	}
	if uRespAfter.Gen <= genBefore {
		t.Fatalf("Expected generation to increment after mtime update (before=%d, after=%d)", genBefore, uRespAfter.Gen)
	}

	// And member001 now has [15, 88]
	uResp1After := svc.HandleHBFLUser(0, "member001")
	if !uResp1After.Success || len(uResp1After.BIDs) != 2 || uResp1After.BIDs[0] != 15 || uResp1After.BIDs[1] != 88 {
		t.Fatalf("Expected member001 to have [15, 88] after VIP update, got %+v", uResp1After)
	}
}

func TestAlohaCooldownAndCache(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "pttbbs_aloha_cooldown_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	writeUserListFile(t, tempDir, "watcher", "alohaed", "flapper\n")

	svc, err := NewService(tempDir)
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	svc.shmClient = nil
	svc.SetAlohaCooldown(60 * time.Second)

	// watcher logs in -> loads alohaed into cache
	svc.HandleUserLogin("watcher", 1001, 1)

	// flapper logs in 1st time -> notifies watcher (1 subscriber)
	r1 := svc.HandleUserLogin("flapper", 2001, 2)
	if !strings.Contains(r1.Message, "notified 1 subscribers") {
		t.Fatalf("Expected 1 subscriber notified on first login, got: %s", r1.Message)
	}
	svc.HandleUserLogout("flapper", 2001)

	// flapper immediately logs in 2nd time within 60s cooldown -> should notify 0 subscribers
	r2 := svc.HandleUserLogin("flapper", 2002, 2)
	if !strings.Contains(r2.Message, "notified 0 subscribers") {
		t.Fatalf("Expected 0 subscribers notified during cooldown, got: %s", r2.Message)
	}
	svc.HandleUserLogout("flapper", 2002)

	// Expire cooldown and log in again -> should notify 1 subscriber again
	svc.SetAlohaCooldown(0)
	r3 := svc.HandleUserLogin("flapper", 2003, 2)
	if !strings.Contains(r3.Message, "notified 1 subscribers") {
		t.Fatalf("Expected 1 subscriber notified after cooldown expired, got: %s", r3.Message)
	}
}

func TestSuperFriendNormalizationAndBackwardCompat(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "pttbbs_super_friend_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	// Alice puts Bob in BOTH overrides and reject (Super Friend)
	writeUserListFile(t, tempDir, "alice", "overrides", "bob super friend\n")
	writeUserListFile(t, tempDir, "alice", "reject", "bob super friend\n")

	svc, err := NewService("")
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	svc.bbsHome = tempDir

	svc.HandleFriendSync("alice", 10, 1001, 5)
	svc.HandleFriendSync("bob", 20, 1002, 12)

	aliceFriends := svc.GetSessionFriendsOnline(5)
	bobFriends := svc.GetSessionFriendsOnline(12)
	if len(aliceFriends) != 1 || len(bobFriends) != 1 {
		t.Fatalf("Expected 1 entry each for Alice and Bob, got %d, %d", len(aliceFriends), len(bobFriends))
	}

	// Normalized unpack: Alice sees IFH|ISH (IRH=0), Bob sees HFM|HSM (HRM=0)
	aStat, _, _ := UnpackFriendOnline(aliceFriends[0])
	if aStat != (FriendBitIFH | FriendBitISH) {
		t.Fatalf("Expected Alice normalized stat IFH|ISH (0x%x), got 0x%x", FriendBitIFH|FriendBitISH, aStat)
	}
	bStat, _, _ := UnpackFriendOnline(bobFriends[0])
	if bStat != (FriendBitHFM | FriendBitHSM) {
		t.Fatalf("Expected Bob normalized stat HFM|HSM (0x%x), got 0x%x", FriendBitHFM|FriendBitHSM, bStat)
	}

	// Raw SHM wire format: keeps IRH/HRM bits set alongside ISH/HSM so legacy mbbsd processes work seamlessly
	aRaw, _, _ := UnpackFriendOnlineRaw(aliceFriends[0])
	if aRaw != (FriendBitIFH | FriendBitIRH | FriendBitISH) {
		t.Fatalf("Expected Alice raw SHM stat IFH|IRH|ISH (0x%x), got 0x%x", FriendBitIFH|FriendBitIRH|FriendBitISH, aRaw)
	}
	bRaw, _, _ := UnpackFriendOnlineRaw(bobFriends[0])
	if bRaw != (FriendBitHFM | FriendBitHRM | FriendBitHSM) {
		t.Fatalf("Expected Bob raw SHM stat HFM|HRM|HSM (0x%x), got 0x%x", FriendBitHFM|FriendBitHRM|FriendBitHSM, bRaw)
	}
}

func TestLegacyCompatCutoffTransition(t *testing.T) {
	origCutoff := LegacyCompatCutoff
	defer func() { LegacyCompatCutoff = origCutoff }()

	tempDir, err := os.MkdirTemp("", "pttbbs_compat_cutoff_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	writeUserListFile(t, tempDir, "alice", "overrides", "bob\n")

	svc, err := NewService("")
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	svc.bbsHome = tempDir
	svc.reconcileInterval = 10 * time.Second
	svc.enableUIDTag = false
	svc.autoLegacyCompat = true
	svc.autoUIDTag = true

	// Simulate time before 2026/09/27 05:10
	LegacyCompatCutoff = time.Now().Add(1 * time.Hour)
	svc.HandleFriendSync("alice", 10, 1001, 5)
	svc.HandleFriendSync("bob", 20, 1002, 12)

	beforeEntries := svc.GetSessionFriendsOnline(5)
	if len(beforeEntries) != 1 {
		t.Fatalf("Expected 1 entry for Alice, got %d", len(beforeEntries))
	}
	_, tagBefore, _ := UnpackFriendOnline(beforeEntries[0])
	if tagBefore != 0 {
		t.Fatalf("Expected uidTag=0 before cutoff, got %d", tagBefore)
	}

	// Simulate time passing 2026/09/27 05:10
	LegacyCompatCutoff = time.Now().Add(-1 * time.Second)
	transitioned, nextInterval := svc.checkLegacyCompatTransition()
	if !transitioned || nextInterval != 1*time.Hour {
		t.Fatalf("Expected transition to 1h, got transitioned=%v nextInterval=%v", transitioned, nextInterval)
	}

	afterEntries := svc.GetSessionFriendsOnline(5)
	if len(afterEntries) != 1 {
		t.Fatalf("Expected 1 entry for Alice after transition, got %d", len(afterEntries))
	}
	_, tagAfter, _ := UnpackFriendOnline(afterEntries[0])
	if tagAfter == 0 {
		t.Fatalf("Expected non-zero uidTag after cutoff transition, got 0")
	}
}

func TestUppercaseHomeAndAlohaAndPriorityTruncation(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "pttbbs_friend_fixes_*")
	if err != nil {
		t.Fatalf("Failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	// 1. Uppercase home directory home/A/Alice
	upperDir := filepath.Join(tempDir, "home", "A", "Alice")
	if err := os.MkdirAll(upperDir, 0755); err != nil {
		t.Fatalf("Failed to mkdir %s: %v", upperDir, err)
	}
	if err := os.WriteFile(filepath.Join(upperDir, "overrides"), []byte("Bob\n"), 0644); err != nil {
		t.Fatalf("Failed to write overrides: %v", err)
	}
	if err := os.WriteFile(filepath.Join(upperDir, "reject"), []byte("Troll\n"), 0644); err != nil {
		t.Fatalf("Failed to write reject: %v", err)
	}
	if err := os.WriteFile(filepath.Join(upperDir, "aloha"), []byte("Bob\nTroll\nAlice\n"), 0644); err != nil {
		t.Fatalf("Failed to write aloha: %v", err)
	}

	svc, err := NewService(tempDir)
	if err != nil {
		t.Fatalf("NewService failed: %v", err)
	}
	svc.enableUIDTag = true
	svc.autoLegacyCompat = false

	svc.HandleFriendSync("Bob", 20, 2001, 10)
	svc.HandleFriendSync("Troll", 30, 3001, 20)
	svc.HandleUserLoginWithUID("Alice", 1001, 5, 10)

	// Verify Alice's uppercase overrides/reject were loaded and applied to Bob (HRM/IRH) & Troll (HRM)
	bobOnline := svc.GetSessionFriendsOnline(10)
	if len(bobOnline) != 1 {
		t.Fatalf("Expected Bob to see 1 entry (Alice), got %d", len(bobOnline))
	}
	flags, _, sid := UnpackFriendOnline(bobOnline[0])
	if sid != 5 || (flags&FriendBitHFM) == 0 {
		t.Fatalf("Expected Bob to have FriendBitHFM from Alice (sid=5), got flags=0x%x sid=%d", flags, sid)
	}

	trollOnline := svc.GetSessionFriendsOnline(20)
	if len(trollOnline) != 1 {
		t.Fatalf("Expected Troll to see 1 entry (Alice FriendBitHRM), got %d", len(trollOnline))
	}
	tFlags, _, tSid := UnpackFriendOnline(trollOnline[0])
	if tSid != 5 || (tFlags&FriendBitHRM) == 0 {
		t.Fatalf("Expected Troll to have FriendBitHRM from Alice (sid=5), got flags=0x%x sid=%d", tFlags, tSid)
	}
}





