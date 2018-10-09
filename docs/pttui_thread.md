pttui_thread
==========

目前是 user-interaction 和 buffer-retrieval 都是在同一個 main-thread 裡運作.
在目前本文和推文都在同一個 file 的情形下. 如此運作是 ok 的.

在之後將 storage 變成 db 的情形下. 本文和推文會被分開儲存. 並且可預期 db 的 access
效能不比 file. 所以希望能夠將 user-interaction 和 buffer-retrieval 分開為兩個 threads.

main-thread 為 user-interaction. 另外有一個 thread (buffer-thread) 為 buffer-retrieval.

在 main-thread 裡: user-interaction 在做完 interaction 以後. 設定預期的 top-window-line 所需要的相關 information. 然後等待 buffer-thread 將 buffer sync 以後 (buffer-state 跟 expected-state 一致), 設定 top-window-line 的 pointer 和相關 information.

在 buffer-thread 裡: 每次的 iteration 會做以下的事情:
1. 得到 expected-top-window-line 相關的 information.
2. 跟既有的 buffer-state (top-window-line) 比較. 一樣的話則直接 return OK.
3. 檢查是否 buffer 裡有 top-window-line. 如果有的話. 則 update top-window-line 和 buffer-state 相關的 information.
4. 如果 top-window-line 不在 buffer 裡的話. 則 resync all buffer. 然後 update top-window-line 和 buffer-state 相關的 information.
5. (這時 main-thread 已經可以知道 buffer-state 有跟 expected-state synced)
6. buffer-thread 根據 top-window-line 和 buffer-top-line 和 buffer-end-line 來決定是否要對於 top-line 和 end-line 做 extend. 並且做相對應的 extension.
