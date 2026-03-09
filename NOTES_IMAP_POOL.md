# IMAP Connection Pool Notes (from review)

Reviewed: `ImapService::getPooledConnection()`, `ImapService::initialize()`, and current callsites.

## Current behavior

- Pool is global/static in `imapservice.cpp` (`g_poolSlots`, `g_poolMutex`, `g_poolWait`).
- `kOperationalPoolMax = 5` (per account, initialized in `initialize()`).
- `initialize()`:
  - registers `g_poolTokenRefresher` callback (refresh token by email);
  - asynchronously prewarms pool entries by account, creating and attempting `connectAndAuth()` for each slot.
- `getPooledConnection(email)`:
  - blocks (up to `kPoolAcquireTimeoutMs = 3500`) until an unbusy slot matching `email` is available;
  - if leased slot is disconnected, refreshes token via callback and reconnects inside `getPooledConnection()`;
  - returns RAII `shared_ptr` lease with custom deleter that marks slot unbusy + wakes waiter.

## Current callsite pattern

Primary callsites currently acquire by account email and then run work using leased connection:

- attachment prefetch paths (image/all)
- folder fetch helpers and refresh paths (`SyncEngine::fetchFolders(pooled, ...)`)
- `fetchFolderHeaders(...)`
- body hydration path (passes pooled cxn into `MessageHydrator::Request`)
- `moveMessage(...)`
- `markMessageRead(...)`

## Design intent implemented

- Pool owns reconnect/auth logic (callers do not call `connectAndAuth()` for pooled leases).
- Lease lifetime controls return-to-pool automatically (RAII deleter).
- IDLE watcher remains separate.

## Things to keep in mind

- Pool initialization is one-time (`g_poolInitialized`), with token refresher callback reset in shutdown.
- `getPooledConnection(email)` filters slots by stored account email; empty email can match first free slot.
- If prewarm connect fails for a slot, reconnect still happens lazily on lease if needed.
