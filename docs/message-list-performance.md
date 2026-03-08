# Message List Performance Notes

## Current strategy (2026-02-25)

- Page size: **50** rows
- Infinite scroll trigger: `bottomGap < 1800`
- Paging fetches use SQL `LIMIT/OFFSET`
- Sort is index-friendly: `ORDER BY cm.received_at DESC` (no `datetime(...)` wrapper)

## Important indexes

Created in `DataStore::init()`:

- `idx_messages_received_at_id` on `messages(received_at DESC, id DESC)`
- `idx_mfm_folder_message` on `message_folder_map(folder, message_id)`
- `idx_mfm_account_message` on `message_folder_map(account_email, message_id)`
- `idx_mfm_account_folder_uid` on `message_folder_map(account_email, folder, uid)`

These were the key fix for All Mail click/scroll hitching.

## Debug UI counters

The message list diagnostic label (`Displayed/Built/Page`) is kept, but only shown in debug builds.

- `main.cpp` exports `kestrelDebugBuild` to QML
- `MessageListPane.qml` shows the counter only when `kestrelDebugBuild == true`

## If performance regresses

1. Run `EXPLAIN QUERY PLAN` on folder-page query.
2. Verify sort still uses `cm.received_at` directly.
3. Verify the above indexes exist in the active DB.
4. Re-check `bottomGap` trigger and page size.
