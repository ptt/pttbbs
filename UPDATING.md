# PTT BBS [Current] Updating Log

This file is encoded in UTF-8.

這裡是 PTT Current 的重大更新記錄，主要是「檔案格式」或位置的重要改變，通常是更新程式碼時要注意一起更新的部份。

跟著 Current 一起升級的朋友們要注意是否有跨過下列的版號，若有請依序手動更新。會列在這裡的版號，強烈建議先整個關站再更新。

> [!NOTE]
> - 最新的版本目前是放在 GitHub 上：[https://github.com/ptt/pttbbs](https://github.com/ptt/pttbbs)
> - `rXXXX` 的版號是 SVN 時代的，目前已沒有簡單方法對應回去。
> - 升級時的檔案很多在 `upgrade/` 目錄底下，若是有 `*.c` 的，多半用 `make XXXX` 就可以編譯出來，如：
>   ```bash
>   make r5858_birth
>   ```

---
## refactor(fileheader_t): Change filemode to 16 bits

這是個可能會造成檔案不相容的修改。
理論上在 Little-endian 架構的機器上沒有問題不用另外轉換，
Big-endian (現代應該幾乎沒有了?) 請自行想辦法。

## [cleanup(recycle): Always enable the time capsule](https://github.com/ptt/pttbbs/commit/2938463053279e682d973a978167967dbc704817)

Time Capsule based 資源回收筒改為預設且唯一的刪除系統。
deleted 跟 junk 只剩保存刪除的精華區目錄所用，暫時還沒有替代方案。

## [feat(2FA): Support two-factor authentication](https://github.com/ptt/pttbbs/commit/12ceb7b6b3e402080204fcada3ad1f41107a33ca)

`userec_t` 的 `_unused4[0]` 被移作 2FA 的狀態，昇級前建議先
確保一下站內帳號的 `_unused4[0]` 都是空的 (0)。

## [`cleanup(mbbsd)`: Remove `m_loginmsg` feature](https://github.com/ptt/pttbbs/commit/184c4bef594b61e2205dc80df1fbd5d427843f4d)

移除了進站水球，請改用編輯進站畫面代替。

## [feat(acl): Make USE_NEW_BAN_SYSTEM official](https://github.com/ptt/pttbbs/commit/2797906a27078de060a24dbb81e6d6a898e09bb2)

配合 Y2038 修正， `USE_NEW_BAN_SYSTEM` 相關條件被移除，未來一律只支援
新式的看板水桶系統。 如果您的系統尚未轉換，請參考
[r5149_waterban.sh](https://github.com/ptt/pttbbs/commit/7099257c55c69b979d4a782c90877100a82ab82e)

## 2026-07-31: [`cleanup(reg)`: make `FOREIGN_REG` always enabled](https://github.com/ptt/pttbbs/commit/c5fb4285f5b6d6994a8e52c6eef5ba88e49133ed)

`FOREIGN_REG` 變成標準行為，且 `FOREIGN_REG_DAY` 相關行為被移除。

## 2026-07-31: [`cleanup(talk)`: remove obsolete `NOKILLWATERBALL` feature](https://github.com/ptt/pttbbs/commit/71f93737f971cb005d5f96f6ad87d417677cbef5)

`NOKILLWATERBALL` 已被移除，如果之前有使用，注意 SHM 大小會有變化要重新 init。正常站台應該沒使用，可正常升級。

> [!WARNING]
> 注意在此 patch 後有不少移除各種不需要的選項的相關修改，多半是不影響系統行為 (`INNTIMEZONE`) 或是只有非常老的系統才有差 (`RFORK`)，或是早已被視為必要的選項 (如 `CONVERT`, `DBCSAWARE`, `BMCHS`, `ALLOW_FREE_TN_ANNOUNCE`, `FOREIGN_REG`)。
> 若系統的新行為不符合您的需求請自行修改。

## 2026-07-28: [`feat(passwd, #112)`: Support long passwords up to 72 chars](https://github.com/ptt/pttbbs/commit/d3d68907893ca321069358edaf01afcf75d69e5f)

密碼系統自此版本開始升級增加 bcrypt, 支援 72 字元內的密碼。

為了避免衝擊使用者已有習慣（主要是利用超過 8 個字元會被忽略來輸入亂數尾碼的人），目前的設定是「如果你設定的是 8 個字元以前的短密碼，一切照舊，輸入超過 8 個字元會被忽略；但如果你設定了 9 個字元以上的長密碼，則未來輸入時完全不會被忽略。」

程式上沒有要設定或升級的地方，但請確實的通知使用者。

另，此 patch 未包含完整使用者界面修正。請一併套用下個 patch:
- [`refactor(ui, #112): Consolidate new password input and confirmation logic`](https://github.com/ptt/pttbbs/commit/2db36a605114d24d745c7a50b7af9c9685506c8a)

## 2026-07-28: [`feat(xchatd)`: Use firstlogin token for xchatd auth](https://github.com/ptt/pttbbs/commit/44493f6131d564d0751821bc4b61a2b4e82e3e2c)

聊天室通訊協定更新，升到此版後請記得要重開 `xchatd` 與所有的 `mbbsd`, 不然會無法進入聊天室。

## 2026-07-27: [`[upgrade]` `cleanup(upgrade)`: Remove outdated upgrade tools](https://github.com/ptt/pttbbs/commit/797ec89a843421d27045a4604449104f2bf33da0)

許多舊的 upgrade script 已移除，如果從非常舊的版本升級，請自行找到還能編譯的中間版本來跑 upgrade。

## 2019-03-14: `[shm]`

`OUTTA_TIMER` 已經被移除。現代作業系統 (至少 Linux 和 FreeBSD) 在各硬體平台上都有支援 vDSO 之類的機制，可以讓 `gettimeofday` 不用 syscall 進 kernel 就取得目前的時間。此機制就如同原先 `OUTTA_TIMER` 的設計，但免卻了我們自己跑 daemon 做同步。

## r5939: `[shm]`

為了減少升級 BBS 時資料結構不相容的情形，SHM 裡的 `now` 現在變為一直都有（即使沒開 `OUTTA_TIMER` 選項）。如果您的 BBS 沒開 `OUTTA_TIMER` 請記得打開後重編 + 重跑，或是手動把 `now` 移掉。

## r5885: `[expire]`

`util/expire` 格式修改，不再看 `days`/`minp`。
為避免誤砍，設定檔也同時改名為 `expire2.conf`。請自行調整。

## r5858: `[birthday]`

不再要求輸入生日，直接要求輸入是否已滿十八歲，減少儲存的使用者個資。
請跑一下 `upgrade/r5858_birth` 用已知生日重建是否滿十八歲的資料。

## r5748: `[typecheck]`

Makefile 現在會多執行 `mbbsd/testsz`，並在其中確保 `userec_t` 等結構大小正確（主要是確認 `time4_t` 與其它自行定義的變數沒有導致 data size 不合）。

## r5734: `[cleanup]`

`userec_t` 裡很多東西以後可能要挪作它用，跑一下 `upgrade/r5734` 可以把資料清空。早作早好。

## r5663: `[fromd/where]`

中文故鄉 (`pttbbs.conf:WHERE`) 改由 `FROMD` 來提供，移除原 cache + `mbbsd` 的機制。
好處是:
1. 減少 reload cache 所花時間
2. 避免 duplicated code

若想使用故鄉功能，請：
1. 編輯 `pttbbs.conf`, 加上 `#define FROMD`
2. `cd ~/pttbbs; make clean; make` (# `mbbsd` 跟 `common` 都要重編)
3. 在開機的 script 加上 `~bbs/daemon/fromd/fromd`（若不想重開機，請順手執行一次上面的命令）
4. `bbsctl restart` ，連線進去測試看看

故鄉的定義檔一樣是 `etc/domain_name_query.cidr` ，格式也一樣。

## r5662: `[banip]`

banip 的 record size 由 `unsigned long` 改為 `in_addr_t`。
在 64-bit 環境上使用的人請全部 (`common`, `util`, `mbbsd`) `make clean` 再 `make all`。
`~bbs/tmp/banip.cache` 也要記得砍掉重建。

## r5653: `[banip]`

`util/banip.pl` 跟 `include/banip.h` 改由 `etc/banip.conf` 取代，以後加 banip 不用再重新編譯程式，只要修改 `banip.conf` 並重啟 bbs (`bbsctl restart`) 即可。
另可將 `etc/banip.conf` 加入 `etc/editable` 方便編輯。

## r5640: `[build]`

Makefile 調整，現在會自動偵測系統內有無 `ccache` 與 `clang`；有的話就自動啟用（`clang` 的優先權高於 `gcc`）。建議使用 `clang 3.0` 以上的版本。
若不想使用 `clang` 請加上參數: `make -DWITHOUT_CLANG`

## r5540: `[configs]`

注意不少 `include/config.h` 的選項改名了，同時也提供了較完善的開關設定方法。
大部份的 LOG 現已集中到 `LOG_CONF_*`。

## r5453: `[ziphome]`

`ZipHome` 增加了 exclude list (範例在 `sample/etc/ziphome.exclude`)。

## r4992: `[ccw chat]`

此版後更新了交談(talk)與聊天室(chat)的核心，另外稅率也在之前的版本有改動。
交談 (talk) 的 protocol 自此版後有所不同，所以升級時要重開。

## r4938: `[remove blog]`

自此版後我們將移除部落格 (blog) 的相關程式碼。
若有此需要的站請自行維護。

## r4886: `[dbcs]`

加了 repeat detection 的 DBCS 還不錯，所以正式脫離使用 detection 界面的日子。
有需要作全站轉換的人請自見拿 `r4871` 去改。

> [!NOTE]
> 或許未來可以把 `DBCSAWARE` 的 conditional compile flag 拿掉

## r4871: `[uflag]`

由於兩個 uflag 實在太容易令人寫錯、而且 uflag 的空間還很大，決定把 `uflag`/`uflag2` 整合。
請注意 `util/bbsmail` 要重 build (因為它會看 `(cuser.uflags2 & REJ_OUTTAMAIL)`)。

## r4848: `[water mode]`

決定把 `uflag2` 的 `WATERMODE` (2 bit) 移出來放到獨立的變數。
`uflag` / `uflag2` 還是放單一 bit 的東西較好。

## r4841: `[shm size tag]`

由於站台設定變動後導致 SHM 大小不同 (eg, `MAX_BOARDS`) 然後有 utility 沒 build 到的問題再次發生，所以我們開啟了 size check。請重 build 所有程式並重開 SHM。

## r4826: `[numlogindays, lastseen]`

`numlogins` 的算法有調整，並且改名為 `numlogindays`；
`lastlogin` 也多了一個叫 `lastseen` (別人 `talk->query` 到的值)。
`lastlogin` 只要登入就一定會更新，`lastseen` 則否。

另，這個 `r4826` 的 upgrade 是 optional 的，不跑也 ok，只是看你要不要一併調整 `numlogin` 的值。

## r4483: `[mbbsd command option]`

從這版開始, `mbbsd` 的 command line 參數改變，使用 `getopt` 處理參數。

原本：
```bash
$ mbbsd 23 3000
```
改成：
```bash
$ mbbsd -d -p 23 -p 3000
```

`mbbsd`, `bbsrf`, `bbsctl` 得一起更新, 並記得 install。
若自己有另外寫 start-up script, 記得修改。

## r4306: `[SHM/from_alias]`

故鄉從此版起有所調整。
原 `from_alias` 改成 `from_ip` 方便直接照 IP 排序。（SHM 大小理應沒有變動）
`currutmp->from` 改為純顯示用。

為正確排序也避免舊 `talk.c` 把 `from_ip` 拿去查 `from_alias` 的表，在安裝此 patch 後請記得重編 `shmctl` 跟 `mbbsd` 並關站後重新執行 `shmctl`。

## r4194: `[PASSWD/STRUCT]`

*** PASSWD 調整：此板把 `r3968` 的空間拿來放職業與電話了，請關站並執行 `upgrade/r4194_passwd` 升級。

> [!IMPORTANT]
> 由此版起，我們把禁止 padding 的宣告加進會寫入 disk 的結構裡。
> 要注意事項是如果你之前已經因為各種原因造成 padding 了，請自行寫轉換程式或是註解掉 `pttstruct.h` 內的 `PACKSTRUCT`。
> 各結構的參考大小都已標上。`mbbsd/testsz.c` 現在可以方便你計算與比較各結構大小。

## r4151: `[REGISTER]`

由於 PTT1/PTT2 轉換測試一切正常，正式改用 Regform v2 的程式碼。
請比照 `r4035` 確定你已經把 `register.new` 給轉移好了。

## r4132: `[REGISTER]`

`upgrade/r4132_reglog2db.py` 可以幫助你把 `register.log` 轉換成 sqlite3 資料庫。
未來可利用這個資料庫進行認證資料的重建。（`r4194` 有較簡易的重建工具）

## r4051: `[CONF]`

所有的 `GLOBAL_*` 板名定義現全改為 `BN_*`。
請記得更新你的 `pttbbs.conf`。

## r4035: `[REGISTER]`

註冊系統導入 Regform v2, 請用 `upgrade/r4035_regnew` 轉移已填註冊單。
(`Regform2` 可由 `USE_REGFORM2` 開啟)

## r3968: `[CHICKEN]`

把 Chicken 搬出 `PASSWD`, 並且改用 `mmap` 同步。
請記得關站後執行 `upgrade/r3968_chicken` 轉移資料後再重開 BBS。

## r3153: `[CHESS]`

chess framework update.

> [!WARNING]
> Chess protocols are NOT backward compatible.
> RESTART WHOLE system to ensure correctness.

## r2459: `[SHM]`

`SHM_t` 增加版本號碼，若版本不合請關站重開。

## r2374: `[SHM]`

把 `ptt.linux` merge 到 trunk。
`SHM_t` 中所有的 pointer 都改成 index 了。
由於這個更動有改到 SHM 的結構，所以請在關站之後再將新版本上線。

## r2366:

trunk 與 stable 第一次分枝。

## r2341: `[SHM]`

`SHM_t` update，為了修某一個 race condition 並拿掉幾個沒在用的欄位。
由於這個更動有改到 SHM 的結構，所以請在關站之後再將新版本上線。

## r2273: `[PASSWDS]`

對於 `userec_t` structure 的一些修改, 以下這些動作得在 bbs 關掉之後進行：
1. 請到 `util/` 下 `make passwdconverter`
2. 執行 `passwdconverter` 會把 `BBSHOME/.PASSWDS` 轉換之後產生 `BBSHOME/.PASSWDS.trans.tmp`
3. 用這個檔蓋掉 `.PASSWDS` 就好了 :)

## r2176: `[SHM]`

`etc/domain_name_query` 改為 `etc/domain_name_query.cidr`。
格式為 CIDR format，您可以直接拿 Ptt/Ptt2 目前所使用的設定檔來用。
由於這個更動有改到 SHM 的結構，所以請在關站之後再將新版本上線。

## r1409: `[etc]`

`expire` 程式修正，原本的用法是 `expire [days [maxp [minp]]]`。
現在透過 `getopt()` 來做，變成 `expire [-d days] [-M maxp] [-m minp] [board names]`。
最後面可以指定傳統一群板名，若不指定的話表示 "全部看板"。
請檢查你的 crontab!

## [from OpenPTT 1.0.2]

`.DIR` 有變，`.BOARDS` 變 `.BRD`, ...
請見 PTT2 PttSrc 板\n
