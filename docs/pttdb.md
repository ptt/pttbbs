pttdb
===========

pttdb 嘗試使用 mongodb 做為 horizontally-scalable 的 db.
目的是希望能夠將目前 main / comment / comment-reply 放在同一個 file 的方式.
refactor 成 main-content, comment, comment-reply 分開儲存的方式.
讓 ptt 可以容易 horizontally-scalable.


Design
========

main:
-----

1. 容許 multi-line.
2. 有一個 main-header. 儲存 main 的 meta-data.
3. 有多個 main-content-block. 儲存 main-content.
4. main-content-block 有自己的 main-content-id.
5. 在 create 時:
    1. 先 create main-header. 並將 status 設成 new.
    2. 將 main-content-block insert 進去.
    3. 將 main-header 設定成 alive.
6. 在 update 時 (預期大約是 create 的 1%):
    1. 將新的 main-content-block insert 進 db.
    2. 將 main-header 的 main-content-id 設定成新的 main-content-id.
    3. remove 舊的 main-content-block (如果要可回溯的話. 就不用 remove 舊的 main-content-block)
7. 在 read 時:
    1. 根據需要的 offset 和 size. 讀 -2n-size ~ 3n-size 和相對應的 blocks.
    2. 如果往上只剩 < -n-size => 再根據現在 offset 和 size. 重新要求 -2n-size ~ 3n-size
    3. 如果往下只剩 < n-size => 再根據現在 offset 和 size. 重新要求 -2n-size ~ 3n-size

comment:
-----

1. 單一 line.
2. 不容許 update.
3. 可以 delete.

comment-reply:
-----

1. 容許 multi-line.
2. 有一個 comment-reply-header. 儲存 comment-reply 的 meta-data.
3. 有多個 comment-reply-block. 儲存 comment-reply-content.
4. comment-reply-block 有自己的 commeny-reply-content-id.