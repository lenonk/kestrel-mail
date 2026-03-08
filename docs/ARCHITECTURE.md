# Kestrel Mail Architecture (Initial)

## Layers

- UI (QML/Kirigami)
- View models / app orchestration
- Core services:
  - accounts
  - auth
  - autodiscover
  - transport (imap/smtp)
  - sync scheduler
  - storage/indexing

## MVP Phases

1. Foundation + app shell
2. Account wizard + OAuth2
3. IMAP sync + local cache
4. SMTP send + outbox queue
5. Threading + search + UX polish

## Non-goals (MVP)

- Exchange EWS graph-native mode
- Calendar/tasks integration
- Plugin ecosystem (post-MVP)
