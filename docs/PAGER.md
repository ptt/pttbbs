# PTT BBS 水球系統 (Pager System) 架構與流程分析

本文件詳細說明 PTT BBS 中的「水球」（即時訊息 / Instant Message）系統運作機制。主要分析檔為 `mbbsd/pager.c`，涵蓋水球的發送 (`my_write`, `ofo_my_write`)、接收 (`write_request_*`, `add_history`)、跨進程 IPC 機制、資料結構、UI 介面模式以及熱鍵 hook 處理。

---

## 1. 概述與總體架構 (Overview & High-Level Architecture)

在 PTT BBS 中，「水球」是線上使用者之間即時傳達的單行簡訊。與需要佔用全螢幕的「聊天室 (Chat)」或「呼叫 (Talk Mode)」不同，水球屬於**事件驅動 (Event-driven)** 的非同步即時訊息，可以在使用者瀏覽看板、閱讀文章、操作選單等各種畫面上方彈出或靜默接收。

### 核心運作模式
1. **多進程架構 (Multi-process Architecture)**：
   每個連線到 BBS 的使用者由獨立的 `mbbsd` 進程處理。
2. **共享記憶體 (Shared Memory, SHM)**：
   所有線上使用者的狀態（包含 PID、呼叫器狀態、未讀水球佇列）皆儲存在全域共享記憶體 `SHM->uinfo[]` (型別為 `userinfo_t`) 中。
3. **UNIX 訊號通知 (`SIGUSR2`)**：
   當發送端丟水球給接收端時，發送端透過 `write_message()` 將訊息寫入接收端在 SHM 中的訊息佇列 `msgs[]`，並向接收端的進程發送 `SIGUSR2` 訊號。接收端收到訊號後觸發 `write_request()` 訊號處理函式，進行音效提示、畫面繪製或寫入歷史紀錄。

```
 [ 發送端 mbbsd (使用者 A) ]                       [ 接收端 mbbsd (使用者 B) ]
 --------------------------                       --------------------------
  1. my_write() 觸發
  2. 檢查發送權限 & 訊息輸入
  3. 檢查接收端狀態 (PAGER_ON?)
  4. 呼叫 write_message() ---------( SHM 原子 CAS 寫入 )----> uin->msgs[pos]
  5. 發送 UNIX 訊號 -----------------( SIGUSR2 )-------------> 6. 觸發 write_request(SIGUSR2)
                                                                 7. show_call_in() 顯示水球
                                                                 8. add_history() 寫入歷史
```

---

## 2. 跨進程 IPC 機制與核心資料結構

### 2.1 核心資料結構 (`include/pttstruct.h`)

#### `msgque_t` (單筆訊息結構)
```c
typedef struct msgque_t {
    pid_t   pid;             // 發送者的 Process ID
    char    userid[IDLEN+1]; // 發送者的帳號 ID (或小天使稱號)
    char    last_call_in[76];// 水球訊息內容 (最多 75 字元)
    int     msgmode;         // 訊息類型 (WRITE, TALK, FROMANGEL, TOANGEL, ALOHA)
} msgque_t;
```

#### `userinfo_t` (共享記憶體中的使用者動態資訊)
位於 SHM `utmpshm->uinfo[]` 中，包含使用者的線上即時狀態：
- `pid_t pid`: 該連線的進程 ID。
- `unsigned char pager`: 呼叫器狀態（`PAGER_ON` / `PAGER_OFF` / `PAGER_DISABLE` / `PAGER_ANTIWB` / `PAGER_FRIENDONLY`）。
- `int msgcount`: 當前未讀水球數量。
- `msgque_t msgs[MAX_MSGS]`: 接收水球的緩衝佇列（預設 `MAX_MSGS = 10`）。

---

## 3. 水球發送流程 (Waterball Sending Flow)

發送水球的主要入口為 `my_write()` 與 `my_write_deliver()`。

### 3.1 核心步驟

1. **檢查自身 Pager 狀態 (`my_write_check_pager_status`)**：
   若發送者自身的 `pager` 處於 `PAGER_OFF` 或 `PAGER_DISABLE`，提示無法發送。
2. **取得訊息輸入 (`my_write_get_input`)**：
   提示使用者輸入單行文字，並去除 ANSI 控碼 (`strip_ansi`)。
3. **二次確認 (`my_write_confirm_send`)**：
   若為第一次丟該使用者，視設定跳出 `[Y/n]` 確認。
4. **校驗接收者狀態與小天使 (`my_write_validate_recipient`)**：
   確認目標使用者存在且登入中。若目標為小天使稱號，自動轉換為小天使真實帳號。
5. **寫入發送端紀錄 (`my_write_log_to_file`)**：
   將發送的水球內容追加寫入發送者的 `water.log`。
6. **判斷接收者是否拒收 (`my_write_is_rejected`)**：
   - `WATERBALL_ALOHA`: 永遠不拒收。
   - 站務員 (SYSOP) / 小天使訊息: 繞過拒收限制。
   - 接收者為 `PAGER_ANTIWB` 或 `PAGER_DISABLE`: 拒收。
   - 接收者為 `PAGER_FRIENDONLY` 且發送者不在對方好友名單: 拒收。
7. **訊息交付與發送 Signal (`my_write_deliver` ➔ `write_message`)**：
   - 轉換發送者 ID (處理 Angel 小天使稱號) 與水球模式 (`msgmode`)。
   - 呼叫通用底層寫入函式 `write_message(uip, uin->pid, currpid, from_id, msg, msgmode)`：
     - 使用原子 Compare-And-Swap (`__sync_bool_compare_and_swap`) 在接收端 SHM (`uentp->msgs[]`) 獨佔可用槽位。
     - 填寫 `pid`, `userid`, `last_call_in`, `msgmode` 等欄位。
     - 以原子方式更新訊息筆數 (`uentp->msgcount`)。
     - 發送 UNIX 訊號: 執行 `kill(uentp->pid, SIGUSR2)` 喚醒接收端。
   - 根據 `write_message()` 回傳值顯示 ANSI 提示訊息（如 「水球砸過去了! *^o^*」 或 「對方不行了!」）。
8. **恢復發送者狀態 (`my_write_restore_state`)**：
   恢復發送者原本的 `currstat`, `chatid` 與 `mode`。

---

## 4. 水球接收與 Signal 處理流程 (Receiving Flow)

### 4.1 訊號註冊與觸發 (`write_request`)

在 `mbbsd.c` 初始化時，進程註冊了 `SIGUSR2` 的 Signal Handler：
```c
signal_restart(SIGUSR2, write_request);
```

當接收端收到 `SIGUSR2` 訊號時，系統呼叫 `write_request(sig)`：

1. **判斷目前 UI 模式**：
   - `PAGER_UI_OFO`: 執行 `write_request_ofo(sig)`。
   - `PAGER_UI_ORIG` / `PAGER_UI_NEW`: 執行 `write_request_default()`。
2. **判斷是否能彈出 UI (`can_pop_pager_ui`)**：
   若使用者正處於文章編輯中 (`EDITING`)、聊天中 (`CHATING`)、呼叫中 (`TALK`) 或已關閉 Pager，系統僅播放音效提示 (`bell()`)，並將水球靜默寫入歷史紀錄。
3. **畫面繪製與音效 (`show_call_in`)**：
   若允許彈出，在螢幕頂端繪製 Top Water Bar（顯示發送者 ID 與訊息），並響鈴提示。
4. **將訊息移入歷史水庫 (`add_history`)**：
   將 `msgs[]` 中的訊息寫入歷史紀錄 `water[]`，並清理/重置 SHM 中的 `msgcount`。

---

## 5. 熱鍵與互動介面 (Key Hooks & Interactive Controls)

水球系統透過 `pager_init_hooks()` 在系統按鍵監聽器中註冊了高優先級的 Modal 按鍵鉤子 (`VKEY_HOOK_PRIO_MODAL`) 與全局呼叫器按鍵鉤子 (`VKEY_HOOK_PRIO_PAGER`)。

### 5.1 按鍵鉤子架構 (`pager_init_hooks`)

按鍵事件分發採用優先級分層處理：
- **`VKEY_HOOK_PRIO_MODAL` ➔ `pager_modal_key_hook()`**：
  僅在彈出水球歷史面板 (`watermode > 0`) 狀態下截獲導覽與切換按鍵 (`Tab`, `Ctrl-T`, `Ctrl-F`, `Ctrl-G`)。
- **`VKEY_HOOK_PRIO_PAGER` ➔ `pager_global_key_hook()`**：
  處理全域呼叫器快捷鍵 (`Ctrl-U`, `Ctrl-R`)，根據 `cuser.pager_ui_type` 分流至對應的 Ctrl-R 處理函式 (`pager_handle_ctrl_r_default` 或 `pager_handle_ctrl_r_ofo`)。

---

### 5.2 核心按鍵功能表

| 按鍵組合 | 作用功能 | 適用介面模式 | 內部處理邏輯 |
| :--- | :--- | :--- | :--- |
| **`Ctrl-U`** | 快速線上使用者列表 | 全部模式 | `pager_handle_ctrl_u()`：暫存畫面 `scr_dump()` ➔ 執行 `t_users()` ➔ 還原畫面 `scr_restore()`。 |
| **`Ctrl-R`** | 查水球 / 回覆水球 | ORIG / NEW | `pager_handle_ctrl_r_default()`：<br>• 第 1 次連按 (收到水球時)：顯示該條訊息並直接開啟 `my_write()` 輸入框回覆。<br>• 第 2 次連按：開啟水球歷史面板 (`watermode = 1`)。<br>• 第 3+ 次連按：切換至更早的水球歷史訊息。 |
| **`Ctrl-R`** | 開啟 OFO 分頁介面 | OFO 模式 | `pager_handle_ctrl_r_ofo()` ➔ 執行 `ofo_my_write()`。 |
| **`Tab` / `Ctrl-T`** | 循環切換歷史水球 | ORIG / NEW | 在 `watermode > 0` 查閱模式下，向前/向後瀏覽歷史訊息。 |
| **`Ctrl-F` / `Ctrl-G`**| 切換對話 User Tab | NEW 模式 | 在 `watermode > 0` 查閱模式下，切換 `swater[0..5]` 不同的對話對象。 |

---

## 6. 水球紀錄與記錄檔管理 (Log Management)

發送或接收水球時，若使用者開啟紀錄功能，系統會將水球內容附加寫入使用者目錄下的 Log 檔案 `water.log` (`fn_writelog`)。

### 歷史紀錄選單 (`pager_show_log()`)

在選單中選取水球紀錄時，會開啟 `pager_show_log()`：
1. 先關閉全域檔案控制代碼 `fp_writelog`（避免檔案 Race Condition）。
2. 呼叫 `more(genbuf, YEA)` 讓使用者以內建閱讀器瀏覽 `water.log`。
3. 退出瀏覽後，下方提示選單：
   - **`(M)` 寄回信箱**：呼叫 `mail_log2id()` 將 `water.log` 作為站內信寄給使用者自己，成功後刪除原始檔。
   - **`(C)` 清除紀錄**：確認後刪除 `water.log` (`unlink(genbuf)`)。
   - **`(R)` 保留紀錄**：維持現狀不變。

---

## 7. 特殊機制與併發同步 (Special Features & Concurrency)

### 7.1 小天使系統 (Little Angel System)
- 水球訊息模式標記為 `MSGMODE_FROMANGEL`（小天使傳給主人）或 `MSGMODE_TOANGEL`（主人傳給小天使）。
- 身分驗證：`my_write_validate_recipient()` 會校驗目標是否為使用者當前的小天使 (`cuser.myangel`)。
- 水球抬頭會特化顯示為 `★小天使: ` 或 `★答小天使: `。

### 7.2 併發寫入與原子 CAS (Atomic Concurrency Control)
- 水球寫入已統一透過 `write_message()` 處理。`write_message()` 採用原子 Compare-And-Swap (`__sync_bool_compare_and_swap`) 機制進行無鎖 slot 搶佔與筆數更新：
  1. 多進程（如多個 `mbbsd` 或 Go 服務 `aloha.svc`）同時寫入時，透過 CPU 級原子指令搶佔 `msgs[i].pid` 槽位，確保不同發送者絕不會搶佔到同一 slot 或發生資料覆蓋。
  2. 填寫完欄位後，再透過 CAS 原子更新 `msgcount` 筆數並發送 `SIGUSR2` 訊號，徹底解決多發送者併發寫入時的競態條件。
