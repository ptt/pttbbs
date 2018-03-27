pttdb
===========

pttdb 嘗試使用 mongodb 做為 horizontally-scalable 的 db.
目的是希望能夠將目前 main / comment / comment-reply 放在同一個 file 的方式.
refactor 成 main-content, comment, comment-reply 分開儲存的方式.
讓 ptt 可以容易 horizontally-scalable.


Design
========

Main
-----
1. 容許 multi-line.
2. 有一個 main-header. 儲存 main 的 meta-data.
3. 有多個 contentBlock. 儲存 main-content.
4. ContentBlock 有自己的 content_id.
5. 在 create 時:
    1. 先 create main-header-id.
    2. 將 content-block insert 進去.
    3. 將 main-header db-update. 並設定成 alive.
6. 在 update 時 (預期大約是 create 的 1%):
    1. 將新的 content-block insert 進 db.
    2. 將 main-header 的 content-id 設定成新的 content-id.
7. 在 read 時:
    1. 根據需要的 offset 和 size. 讀 -2n-size ~ 3n-size 和相對應的 blocks.
    2. 如果往上只剩 < -n-size => 再根據現在 offset 和 size. 重新要求 -2n-size ~ 3n-size
    3. 如果往下只剩 < n-size => 再根據現在 offset 和 size. 重新要求 -2n-size ~ 3n-size

ContentBlock
-----
1. buffer default 是 NULL pointer. 使用 init_content_block_buf_block 來 init buffer. 使用 destroy_content_block 來 free buffer.
2. 可使用 associate_content_block 來 associate 既有的 buffer. 使用 dissociate_content_block 來重新設定為 NULL (不會 free buffer).
3. 可使用 dynamic_read_content_blocks 來將連續的 content_block 在 read content 時 assign 到連續的 buffer 裡.

Comment
-----
1. buffer default 是 NULL pointer. 使用 init_comment_buf 來 init buffer. 使用 destroy_comment 來 free buffer.
2. 可使用 associate_comment 來 associate 既有的 buffer. 使用 dissociate_comment 來重新設定為 NULL (不會 free buffer).
3. 單一 line.
4. 不容許 update.
5. 可以 delete.

CommentReply
-----
1. buffer default 是 NULL pointer. 使用 init_comment_reply_buf 來 init buffer. 使用 destroy_comment_reply 來 free buffer.
2. 可使用 associate_comment_reply 來 associate 既有的 buffer. 使用 dissociate_comment_reply 來重新設定為 NULL (不會 free buffer).
3. 容許 multi-line.
4. 考量到效能. comment_reply_id 目前跟 comment_id 是一致的. (不用考慮 multi-comment-reply records 問題)
5. 目前 update 是直接覆蓋舊的.