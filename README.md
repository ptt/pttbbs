# PTT BBS (pttbbs)

批踢踢實業坊 (PTT BBS) 核心系統原始碼。

## 快速入門與文件

- **快速安裝**：請參考 [`docs/INSTALL`](docs/INSTALL) 以及 [`docs/FAQ`](docs/FAQ)
- **詳細文件**：請見 [`docs/`](docs/) 目錄或查閱 [PttBBS Wiki](https://github.com/ptt/pttbbs/wiki)
- **問題回報與討論**：
  - 批踢踢實業坊：`telnet://ptt.cc` 的 `PttCurrent` 看板
  - 線上瀏覽看板：[PttCurrent 看板](https://www.ptt.cc/bbs/PttCurrent/index.html) 與 [精華區](https://www.ptt.cc/man/PttCurrent/index.html)

---

## 目錄結構說明

### 根目錄與說明文件

- **`LICENSE`**：本軟體各檔案在未另外指定時的授權方式（GPL v2）。請注意部份檔案使用不同的授權（如 BSD License）。由於 GPL 的限制，授權不相容的程式碼已預設為不使用，並提供 GPL 相容版本的替代用程式碼以維持功能完整，詳情與設定請參見各檔案內文。
- **`UPDATING.md`**：重大更新與版本升級說明紀錄
- **`docs/`**：各項文件說明目錄
  - [`ADVANCE`](docs/ADVANCE)：進階功能說明 (bbsctl, shmctl 等)
  - [`ANCESTOR`](docs/ANCESTOR)：沿承歷史與發展腳步
  - [`DONATE`](docs/DONATE)：贊助方式說明
  - [`FAQ`](docs/FAQ)：常見問題（如 `sendmail.cf` 設定方法等）
  - [`INSTALL`](docs/INSTALL)：快速安裝指引
  - [`proto/`](docs/proto/)：`mbbsd/` 內部各模組與通訊協定說明（詳見該目錄下的 [`README`](docs/proto/README)）
  - `z6ibbs.[12].txt`：in2 站長隨筆與設計筆記（[`z6ibbs.1.txt`](docs/z6ibbs.1.txt) / [`z6ibbs.2.txt`](docs/z6ibbs.2.txt)）

---

### 設定與範例檔 (`sample/`)

- `crontab`：提供 BBS 執行時需透過 crontab 定時排程執行的範例設定
- `pttbbs.sh`：FreeBSD rc 自動啟動服務範例 (`/usr/local/etc/rc.d`)
- `rc.local`：Linux rc 自動啟動服務範例 (`/etc/rc.local`)
- `pttbbs.conf`：完整範例設定檔
- `pttbbs_minimal.conf`：最小化範例設定檔

---

### 核心程式與目錄

- **`include/`**：標頭檔 (Header files)
- **`common/`**：跨模組共用程式庫 (Common library)
- **`mbbsd/`**：BBS 文字模式 (Terminal / Telnet) 主程式
- **`daemon/`**：背景服務與輔助伺服程式
  - `angelbeats/`：小天使相關服務
  - `banipd/`：IP 封鎖動態判斷服務 (experimental)
  - `barebone/`：Daemon 伺服程式骨架
  - `boardd/`：看板文章服務 (for Web)
  - `brcstored/`：BRC 儲存服務 (failed experimental)
  - `commentd/`：推文記錄服務 (experimental)
  - `fromd/`：故鄉 (IP/Domain) 查詢服務
  - `logind/`：海量登入前導與連線分流服務
  - `mand/`：精華區文章服務 (for Web)
  - `postd/`：文章記錄服務 (experimental)
  - `regmaild/`：註冊 Email 驗證與寄送服務
  - `utmpd/`：UTMP 快取伺服器 (experimental)
  - `wsproxy/`：WebSocket 至 Telnet BBS 代理轉接服務 (experimental)\n