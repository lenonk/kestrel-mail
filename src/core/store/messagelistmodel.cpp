#include "messagelistmodel.h"

#include "datastore.h"

#include <algorithm>
#include <QDate>
#include <QDateTime>
#include <QSet>
#include <QTimer>
#include <QLocale>
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrentRun>

using namespace Qt::Literals::StringLiterals;

namespace {

QString bucketKeyForDate(const QString &dateValue)
{
    const QDateTime dt = QDateTime::fromString(dateValue, Qt::ISODate);
    if (!dt.isValid()) return "older"_L1;
    const QDate target = dt.toLocalTime().date();
    const QDate today = QDate::currentDate();
    const int diffDays = target.daysTo(today);
    if (diffDays <= 0) return "today"_L1;
    if (diffDays == 1) return "yesterday"_L1;

    const QDate weekStart = today.addDays(-(today.dayOfWeek() % 7)); // Sunday-start week
    if (target >= weekStart && target < today) {
        return QStringLiteral("weekday-%1").arg(target.dayOfWeek());
    }

    if (diffDays <= 14) return "lastWeek"_L1;
    if (diffDays <= 21) return "twoWeeksAgo"_L1;
    return "older"_L1;
}

QString bucketLabel(const QString &bucketKey)
{
    if (bucketKey == "today"_L1) return "Today"_L1;
    if (bucketKey == "yesterday"_L1) return "Yesterday"_L1;
    if (bucketKey.startsWith("weekday-"_L1)) {
        bool ok = false;
        const int dow = bucketKey.mid("weekday-"_L1.size()).toInt(&ok);
        if (ok && dow >= 1 && dow <= 7) {
            return QLocale().dayName(dow, QLocale::LongFormat);
        }
    }
    if (bucketKey == "lastWeek"_L1) return "Last Week"_L1;
    if (bucketKey == "twoWeeksAgo"_L1) return "Two Weeks Ago"_L1;
    return "Older"_L1;
}

QString messageStableKey(const QVariantMap &row)
{
    const QString account = row.value("accountEmail"_L1).toString();
    const QString folder = row.value("folder"_L1).toString();
    const QString uid = row.value("uid"_L1).toString();
    if (!account.isEmpty() && !folder.isEmpty() && !uid.isEmpty()) {
        return "msg:%1|%2|%3"_L1.arg(account, folder, uid);
    }
    return "msg:%1|%2|%3|%4|%5"_L1
            .arg(account,
                 row.value("uid"_L1).toString(),
                 row.value("receivedAt"_L1).toString(),
                 row.value("sender"_L1).toString(),
                 row.value("subject"_L1).toString());
}

} // namespace

MessageListModel::MessageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_reloadDebounceTimer = new QTimer(this);
    m_reloadDebounceTimer->setSingleShot(true);
    m_reloadDebounceTimer->setInterval(80);
    connect(m_reloadDebounceTimer, &QTimer::timeout, this, &MessageListModel::refresh);
}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    QMutexLocker locker(&m_rowsMutex);
    return m_rows.size();
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    QMutexLocker locker(&m_rowsMutex);
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case RowTypeRole: return row.type;
    case IsHeaderRole: return row.type == HeaderRow;
    case HasTopGapRole: return row.hasTopGap;
    case BucketKeyRole: return row.bucketKey;
    case TitleRole: return row.title;
    case ExpandedRole: return row.expanded;
    case MessageKeyRole: return row.stableKey;
    case AccountEmailRole: return row.message.value("accountEmail"_L1);
    case FolderRole: return row.message.value("folder"_L1);
    case UidRole: return row.message.value("uid"_L1);
    case SenderRole: return row.message.value("sender"_L1);
    case RecipientRole: return row.message.value("recipient"_L1);
    case SubjectRole: return row.message.value("subject"_L1);
    case ReceivedAtRole: return row.message.value("receivedAt"_L1);
    case SnippetRole: return row.message.value("snippet"_L1);
    case AvatarDomainRole: return row.message.value("avatarDomain"_L1);
    case AvatarUrlRole: return row.message.value("avatarUrl"_L1);
    case AvatarSourceRole: return row.message.value("avatarSource"_L1);
    case UnreadRole: return row.message.value("unread"_L1);
    case HasAttachmentsRole: return row.message.value("hasAttachments"_L1);
    case HasTrackingPixelRole: return row.message.value("hasTrackingPixel"_L1);
    case ThreadCountRole: return row.message.value("threadCount"_L1);
    case IsImportantRole: return row.message.value("isImportant"_L1);
    case AllSendersRole:  return row.message.value("allSenders"_L1);
    case FlaggedRole:          return row.message.value("flagged"_L1);
    case IsSearchResultRole:   return row.message.value("isSearchResult"_L1);
    case ResultFolderRole:     return row.message.value("folder"_L1);
    default: return {};
    }
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {
        { RowTypeRole, QByteArrayLiteral("rowType") },
        { IsHeaderRole, QByteArrayLiteral("isHeader") },
        { HasTopGapRole, QByteArrayLiteral("hasTopGap") },
        { BucketKeyRole, QByteArrayLiteral("bucketKey") },
        { TitleRole, QByteArrayLiteral("title") },
        { ExpandedRole, QByteArrayLiteral("expanded") },
        { MessageKeyRole, QByteArrayLiteral("messageKey") },
        { AccountEmailRole, QByteArrayLiteral("accountEmail") },
        { FolderRole, QByteArrayLiteral("folder") },
        { UidRole, QByteArrayLiteral("uid") },
        { SenderRole, QByteArrayLiteral("sender") },
        { RecipientRole, QByteArrayLiteral("recipient") },
        { SubjectRole, QByteArrayLiteral("subject") },
        { ReceivedAtRole, QByteArrayLiteral("receivedAt") },
        { SnippetRole, QByteArrayLiteral("snippet") },
        { AvatarDomainRole, QByteArrayLiteral("avatarDomain") },
        { AvatarUrlRole, QByteArrayLiteral("avatarUrl") },
        { AvatarSourceRole, QByteArrayLiteral("avatarSource") },
        { UnreadRole, QByteArrayLiteral("unread") },
        { HasAttachmentsRole, QByteArrayLiteral("hasAttachments") },
        { HasTrackingPixelRole, QByteArrayLiteral("hasTrackingPixel") },
        { ThreadCountRole, QByteArrayLiteral("threadCount") },
        { IsImportantRole, QByteArrayLiteral("isImportant") },
        { AllSendersRole,  QByteArrayLiteral("allSenders") },
        { FlaggedRole,          QByteArrayLiteral("flagged") },
        { IsSearchResultRole,   QByteArrayLiteral("isSearchResult") },
        { ResultFolderRole,     QByteArrayLiteral("resultFolder") }
    };
}

DataStore *MessageListModel::dataStore() const
{
    return m_dataStore.data();
}

void MessageListModel::setDataStore(DataStore *store)
{
    if (m_dataStore == store) return;
    if (m_dataStore) {
        disconnect(m_dataStore, nullptr, this, nullptr);
    }
    m_dataStore = store;
    if (m_dataStore) {
        connect(m_dataStore, &DataStore::dataChanged, this, &MessageListModel::scheduleRefresh);
        connect(m_dataStore, &DataStore::messageMarkedRead, this, &MessageListModel::onMessageMarkedRead);
        connect(m_dataStore, &DataStore::messageFlaggedChanged, this, &MessageListModel::onMessageFlaggedChanged);
        connect(m_dataStore, &QObject::destroyed, this, [this]() {
            m_dataStore = nullptr;
            refresh();
            emit dataStoreChanged();
        });
    }
    refresh();
    emit dataStoreChanged();
}

void MessageListModel::setSelection(const QString &folderKey, const QStringList &selectedCategories, int selectedCategoryIndex)
{
    const int normalizedIndex = qMax(0, selectedCategoryIndex);
    if (m_folderKey == folderKey && m_selectedCategories == selectedCategories && m_selectedCategoryIndex == normalizedIndex) {
        return;
    }
    m_folderKey = folderKey;
    m_selectedCategories = selectedCategories;
    m_selectedCategoryIndex = normalizedIndex;
    m_windowStart = 0;
    m_loadedSourceRows.clear();
    m_nextOffset = 0;
    if (m_hasMore) {
        m_hasMore = false;
        emit pagingChanged();
    }
    if (!m_searchQuery.isEmpty()) return; // search takes priority over folder selection
    refresh();
}

void MessageListModel::setSearchQuery(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (m_searchQuery == trimmed) return;
    m_searchQuery = trimmed;
    m_windowStart = 0;
    m_loadedSourceRows.clear();
    m_nextOffset = 0;
    if (m_hasMore) {
        m_hasMore = false;
        emit pagingChanged();
    }
    emit searchQueryChanged();
    refresh();
}

void MessageListModel::setBucketExpanded(const QString &bucketKey, bool expanded)
{
    // Expansion state only affects the view — no DB re-query needed.
    const QString key = bucketKey.trimmed();
    bool changed = false;
    if (key == "today"_L1) {
        changed = (m_todayExpanded != expanded);
        m_todayExpanded = expanded;
    } else if (key == "yesterday"_L1) {
        changed = (m_yesterdayExpanded != expanded);
        m_yesterdayExpanded = expanded;
    } else if (key == "lastWeek"_L1) {
        changed = (m_lastWeekExpanded != expanded);
        m_lastWeekExpanded = expanded;
    } else if (key == "twoWeeksAgo"_L1) {
        changed = (m_twoWeeksAgoExpanded != expanded);
        m_twoWeeksAgoExpanded = expanded;
    } else if (key == "older"_L1) {
        changed = (m_olderExpanded != expanded);
        m_olderExpanded = expanded;
    } else if (key.startsWith("weekday-"_L1)) {
        const bool prev = m_weekdayExpanded.value(key, true);
        changed = (prev != expanded);
        m_weekdayExpanded.insert(key, expanded);
    }
    if (!changed) return;
    refreshView();

    // Collapsing a bucket shrinks the visible list. Load more messages from
    // the DB to fill the gap so the user doesn't see an unexpectedly short list.
    if (!expanded && m_hasMore)
        loadMore();
}

void MessageListModel::setExpansionState(bool todayExpanded,
                                         bool yesterdayExpanded,
                                         bool lastWeekExpanded,
                                         bool twoWeeksAgoExpanded,
                                         bool olderExpanded)
{
    if (m_todayExpanded == todayExpanded
        && m_yesterdayExpanded == yesterdayExpanded
        && m_lastWeekExpanded == lastWeekExpanded
        && m_twoWeeksAgoExpanded == twoWeeksAgoExpanded
        && m_olderExpanded == olderExpanded) {
        return;
    }
    m_todayExpanded = todayExpanded;
    m_yesterdayExpanded = yesterdayExpanded;
    m_lastWeekExpanded = lastWeekExpanded;
    m_twoWeeksAgoExpanded = twoWeeksAgoExpanded;
    m_olderExpanded = olderExpanded;
    refreshView();
}

void MessageListModel::refresh()
{
    if (m_isLoadingPage) {
        m_pendingRefresh = true;
        return;
    }
    m_isLoadingPage = true;
    m_pendingRefresh = false;

    DataStore *store = m_dataStore.data();
    if (!store) {
        m_loadedSourceRows = {};
        m_nextOffset = 0;
        refreshView();
        m_isLoadingPage = false;
        return;
    }

    const int preserveLimit = qMax(m_pageSize, m_nextOffset);
    const QString folderKey = m_folderKey;
    const QStringList selectedCategories = m_selectedCategories;
    const int selectedCategoryIndex = m_selectedCategoryIndex;

    using Result = std::pair<QVariantList, bool>;
    auto *watcher = new QFutureWatcher<Result>(this);
    m_refreshWatcher = watcher;

    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher]() {
        m_refreshWatcher = nullptr;
        watcher->deleteLater();

        QElapsedTimer t; t.start();
        const auto [rows, hasMore] = watcher->result();
        m_loadedSourceRows = rows;
        m_nextOffset = m_loadedSourceRows.size();

        const bool oldHasMore = m_hasMore;
        m_hasMore = hasMore;
        if (oldHasMore != m_hasMore) emit pagingChanged();

        refreshView();
        m_isLoadingPage = false;

        if (m_pendingRefresh) {
            m_pendingRefresh = false;
            refresh();
        }
    });

    const QString searchQuery = m_searchQuery;
    watcher->setFuture(QtConcurrent::run(
        [store, folderKey, selectedCategories, selectedCategoryIndex, preserveLimit, searchQuery]() -> Result {
            QElapsedTimer t; t.start();
            bool hasMore = false;
            QVariantList rows;
            if (!searchQuery.isEmpty()) {
                rows = store->searchMessages(searchQuery, preserveLimit, 0, &hasMore);
            } else {
                rows = store->messagesForSelection(
                    folderKey, selectedCategories, selectedCategoryIndex, preserveLimit, 0, &hasMore);
            }
            return {std::move(rows), hasMore};
        }));
}

void MessageListModel::refreshView()
{
    QVector<Row> built = buildRows(m_loadedSourceRows);
    {
        QMutexLocker locker(&m_rowsMutex);
        m_allRows = std::move(built);
    }
    emit totalRowCountChanged();
    applyWindow();
}

void MessageListModel::applyWindow()
{
    QVector<Row> window;
    {
        QMutexLocker locker(&m_rowsMutex);
        if (m_windowStart < 0) m_windowStart = 0;

        const int effectiveWindowSize = (m_windowSize <= 0) ? m_allRows.size() : m_windowSize;
        if (m_windowStart >= m_allRows.size()) m_windowStart = qMax(0, m_allRows.size() - effectiveWindowSize);

        const int end = qMin(m_allRows.size(), m_windowStart + effectiveWindowSize);
        for (int i = m_windowStart; i < end; ++i) window.push_back(m_allRows.at(i));
    }
    applyRows(std::move(window));
}

void MessageListModel::loadMore()
{
    if (!m_hasMore || m_isLoadingPage) {
        return;
    }

    m_isLoadingPage = true;

    QVariantList nextRows;
    bool hasMore = false;
    if (m_dataStore) {
        nextRows = m_dataStore->messagesForSelection(m_folderKey, m_selectedCategories, m_selectedCategoryIndex, m_pageSize, m_nextOffset, &hasMore);
    }

    if (!nextRows.isEmpty()) {
        m_loadedSourceRows.reserve(m_loadedSourceRows.size() + nextRows.size());
        for (const QVariant &v : nextRows) m_loadedSourceRows.push_back(v);
        m_nextOffset += nextRows.size();
    }

    const bool oldHasMore = m_hasMore;
    m_hasMore = hasMore;
    if (oldHasMore != m_hasMore) emit pagingChanged();

    QVector<Row> window;
    if (!nextRows.isEmpty()) {
        const QVector<Row> deltaRows = buildRows(nextRows);
        QMutexLocker locker(&m_rowsMutex);
        QSet<QString> existingHeaders;
        QSet<QString> existingMessages;
        for (const Row &r : m_allRows) {
            if (r.type == HeaderRow) existingHeaders.insert(r.bucketKey);
            else existingMessages.insert(r.stableKey);
        }

        for (const Row &r : deltaRows) {
            if (r.type == HeaderRow) {
                if (existingHeaders.contains(r.bucketKey)) continue;
                existingHeaders.insert(r.bucketKey);
                Row copy = r;
                copy.hasTopGap = !m_allRows.isEmpty();
                m_allRows.push_back(copy);
            } else {
                if (existingMessages.contains(r.stableKey)) continue;
                existingMessages.insert(r.stableKey);
                m_allRows.push_back(r);
            }
        }
    }

    {
        QMutexLocker locker(&m_rowsMutex);
        if (m_windowStart < 0) m_windowStart = 0;
        const int effectiveWindowSize = (m_windowSize <= 0) ? m_allRows.size() : m_windowSize;
        if (m_windowStart >= m_allRows.size()) m_windowStart = qMax(0, m_allRows.size() - effectiveWindowSize);

        const int end = qMin(m_allRows.size(), m_windowStart + effectiveWindowSize);
        for (int i = m_windowStart; i < end; ++i) window.push_back(m_allRows.at(i));
    }

    emit totalRowCountChanged();
    applyRows(std::move(window));

    m_isLoadingPage = false;
}

void MessageListModel::scheduleRefresh()
{
    if (!m_reloadDebounceTimer) {
        refresh();
        return;
    }
    // Throttle (not debounce): avoid resetting timer on every insert,
    // so list can update progressively during long fetches.
    if (!m_reloadDebounceTimer->isActive()) {
        m_reloadDebounceTimer->start();
    }
}

void MessageListModel::onMessageMarkedRead(const QString &accountEmail, const QString &uid)
{
    // Directly update the unread role for any visible row matching this account+uid.
    // This bypasses the 80ms refresh throttle so the unread dot disappears immediately.
    // The subsequent full refresh (from inboxChanged) will reconcile m_loadedSourceRows.
    int changedIndex = -1;
    {
        QMutexLocker locker(&m_rowsMutex);
        for (int i = 0; i < m_rows.size(); ++i) {
            Row &row = m_rows[i];
            if (row.type != MessageRow) continue;
            if (row.message.value("accountEmail"_L1).toString() != accountEmail) continue;
            if (row.message.value("uid"_L1).toString() != uid) continue;
            if (row.message.value("unread"_L1).toInt() == 0) continue;
            row.message.insert("unread"_L1, 0);
            changedIndex = i;
            break;
        }
        // Also patch m_allRows so the window doesn't restore the stale value on the next scroll.
        for (Row &row : m_allRows) {
            if (row.type == MessageRow
                    && row.message.value("accountEmail"_L1).toString() == accountEmail
                    && row.message.value("uid"_L1).toString() == uid) {
                row.message.insert("unread"_L1, 0);
                break;
            }
        }
    }
    if (changedIndex >= 0)
        emit dataChanged(index(changedIndex), index(changedIndex), {UnreadRole});
}

void MessageListModel::onMessageFlaggedChanged(const QString &accountEmail, const QString &uid, bool flagged)
{
    const int newFlagged = flagged ? 1 : 0;
    int changedIndex = -1;
    {
        QMutexLocker locker(&m_rowsMutex);
        for (int i = 0; i < m_rows.size(); ++i) {
            Row &row = m_rows[i];
            if (row.type != MessageRow) continue;
            if (row.message.value("accountEmail"_L1).toString() != accountEmail) continue;
            if (row.message.value("uid"_L1).toString() != uid) continue;
            if (row.message.value("flagged"_L1).toInt() == newFlagged) continue;
            row.message.insert("flagged"_L1, newFlagged);
            changedIndex = i;
            break;
        }
        for (Row &row : m_allRows) {
            if (row.type == MessageRow
                    && row.message.value("accountEmail"_L1).toString() == accountEmail
                    && row.message.value("uid"_L1).toString() == uid) {
                row.message.insert("flagged"_L1, newFlagged);
                break;
            }
        }
    }
    if (changedIndex >= 0)
        emit dataChanged(index(changedIndex), index(changedIndex), {FlaggedRole});
}

void MessageListModel::setWindowSize(int size)
{
    // 0 means unlimited/full list. Positive values are bounded to sane minimum.
    const int normalized = (size <= 0) ? 0 : qMax(40, size);
    if (m_windowSize == normalized) return;
    m_windowSize = normalized;
    emit windowSizeChanged();
    applyWindow();
}

void MessageListModel::shiftWindowDown()
{
    {
        QMutexLocker locker(&m_rowsMutex);
        if (m_allRows.isEmpty()) return;
        if (m_windowSize <= 0) return; // unlimited mode
        const int step = qMax(10, m_windowSize / 2);
        const int maxStart = qMax(0, m_allRows.size() - m_windowSize);
        const int next = qMin(maxStart, m_windowStart + step);
        if (next == m_windowStart) return;
        m_windowStart = next;
    }
    applyWindow();
}

void MessageListModel::shiftWindowUp()
{
    {
        QMutexLocker locker(&m_rowsMutex);
        if (m_allRows.isEmpty()) return;
        if (m_windowSize <= 0) return; // unlimited mode
        const int step = qMax(10, m_windowSize / 2);
        const int next = qMax(0, m_windowStart - step);
        if (next == m_windowStart) return;
        m_windowStart = next;
    }
    applyWindow();
}

QVector<MessageListModel::Row> MessageListModel::buildRows(const QVariantList &rows) const
{
    QVector<Row> out;
    QVariantList workingRows = rows;

    if (workingRows.isEmpty()) {
        const bool inboxSelected = m_folderKey.compare("account:inbox"_L1, Qt::CaseInsensitive) == 0;
        const bool categoryTabActive = !m_selectedCategories.isEmpty();
        if (inboxSelected && !categoryTabActive) {
            QVariantMap mock;
            mock.insert("accountEmail"_L1, QString());
            mock.insert("folder"_L1, "INBOX"_L1);
            mock.insert("uid"_L1, "mock"_L1);
            mock.insert("sender"_L1, "welcome@kestrel.mail"_L1);
            mock.insert("subject"_L1, "Welcome to Kestrel Mail"_L1);
            mock.insert("snippet"_L1, "Your account is set up. Press Refresh to load real mail from your provider."_L1);
            mock.insert("receivedAt"_L1, "2026-02-17T11:00:00"_L1);
            mock.insert("unread"_L1, true);
            workingRows.push_back(mock);
        }
    }

    QHash<QString, QList<QVariantMap>> buckets;
    buckets.insert("today"_L1, {});
    buckets.insert("yesterday"_L1, {});
    buckets.insert("lastWeek"_L1, {});
    buckets.insert("twoWeeksAgo"_L1, {});
    buckets.insert("older"_L1, {});

    const QDate today = QDate::currentDate();
    const QDate weekStart = today.addDays(-(today.dayOfWeek() % 7)); // Sunday-start week
    QStringList weekdayOrder;
    for (QDate d = today.addDays(-2); d >= weekStart; d = d.addDays(-1)) {
        const QString key = QStringLiteral("weekday-%1").arg(d.dayOfWeek());
        if (!weekdayOrder.contains(key)) {
            weekdayOrder.push_back(key);
            buckets.insert(key, {});
        }
    }

    QSet<QString> dedupe;
    for (const QVariant &v : workingRows) {
        const QVariantMap map = v.toMap();
        const QString stable = messageStableKey(map);
        if (dedupe.contains(stable)) continue;
        dedupe.insert(stable);
        const QString bucket = bucketKeyForDate(map.value("receivedAt"_L1).toString());
        buckets[bucket].push_back(map);
    }

    QStringList order;
    order << "today"_L1 << "yesterday"_L1;
    order << weekdayOrder;
    order << "lastWeek"_L1 << "twoWeeksAgo"_L1 << "older"_L1;

    for (const QString &bucket : order) {
        const QList<QVariantMap> rowsInBucket = buckets.value(bucket);
        if (rowsInBucket.isEmpty()) continue;

        Row header;
        header.type = HeaderRow;
        header.bucketKey = bucket;
        header.title = bucketLabel(bucket);
        header.expanded = bucketExpanded(bucket);
        header.hasTopGap = !out.isEmpty();
        header.stableKey = "header:%1"_L1.arg(bucket);
        out.push_back(header);

        if (!header.expanded) continue;

        for (const QVariantMap &message : rowsInBucket) {
            Row row;
            row.type = MessageRow;
            row.stableKey = messageStableKey(message);
            row.message = message;
            out.push_back(row);
        }
    }

    return out;
}

int MessageListModel::totalRowCount() const
{
    QMutexLocker locker(&m_rowsMutex);
    return m_allRows.size();
}

int MessageListModel::visibleRowCount() const
{
    QMutexLocker locker(&m_rowsMutex);
    return m_rows.size();
}

int MessageListModel::visibleMessageCount() const
{
    QMutexLocker locker(&m_rowsMutex);
    int count = 0;
    for (const Row &r : m_rows) {
        if (r.type == MessageRow) ++count;
    }
    return count;
}

QVariantMap MessageListModel::rowAt(int index) const
{
    QMutexLocker locker(&m_rowsMutex);
    if (index < 0 || index >= m_rows.size()) return {};
    const Row &r = m_rows.at(index);
    QVariantMap result;
    result.insert("isHeader"_L1, r.type == HeaderRow);
    result.insert("messageKey"_L1, r.stableKey);
    return result;
}

void MessageListModel::applyRows(QVector<Row> &&nextRows)
{
    QMutexLocker locker(&m_rowsMutex);

    if (m_rows.isEmpty() && nextRows.isEmpty()) return;

    if (m_rows.isEmpty()) {
        beginInsertRows(QModelIndex(), 0, nextRows.size() - 1);
        m_rows = std::move(nextRows);
        endInsertRows();
        locker.unlock();
        emit visibleCountsChanged();
        return;
    }

    if (nextRows.isEmpty()) {
        beginRemoveRows(QModelIndex(), 0, m_rows.size() - 1);
        m_rows.clear();
        endRemoveRows();
        locker.unlock();
        emit visibleCountsChanged();
        return;
    }

    const int oldSize = m_rows.size();
    const int newSize = nextRows.size();

    if (oldSize == newSize) {
        bool sameKeys = true;
        for (int i = 0; i < oldSize; ++i) {
            if (m_rows.at(i).stableKey != nextRows.at(i).stableKey) {
                sameKeys = false;
                break;
            }
        }
        if (sameKeys) {
            for (int i = 0; i < oldSize; ++i) {
                const Row &oldRow = m_rows.at(i);
                const Row &newRow = nextRows.at(i);
                if (!rowEquals(oldRow, newRow)) {
                    m_rows[i] = newRow;
                    const QList<int> roles = changedRoles(oldRow, newRow);
                    emit dataChanged(index(i), index(i), roles);
                }
            }
            return;
        }
    }

    int prefix = 0;
    while (prefix < oldSize && prefix < newSize && m_rows.at(prefix).stableKey == nextRows.at(prefix).stableKey) {
        ++prefix;
    }

    int suffix = 0;
    while (suffix < (oldSize - prefix)
           && suffix < (newSize - prefix)
           && m_rows.at(oldSize - 1 - suffix).stableKey == nextRows.at(newSize - 1 - suffix).stableKey) {
        ++suffix;
    }

    const int oldMid = oldSize - prefix - suffix;
    const int newMid = newSize - prefix - suffix;

    // Update prefix rows in-place and emit dataChanged so QML sees the new
    // values (e.g. a header's expanded state) before the structural change.
    for (int i = 0; i < prefix; ++i) {
        if (!rowEquals(m_rows.at(i), nextRows.at(i))) {
            const QList<int> roles = changedRoles(m_rows.at(i), nextRows.at(i));
            m_rows[i] = nextRows.at(i);
            emit dataChanged(index(i), index(i), roles);
        }
    }

    if (oldMid == 0 && newMid > 0) {
        beginInsertRows(QModelIndex(), prefix, prefix + newMid - 1);
        m_rows = std::move(nextRows);
        endInsertRows();
        locker.unlock();
        emit visibleCountsChanged();
        return;
    }

    if (newMid == 0 && oldMid > 0) {
        beginRemoveRows(QModelIndex(), prefix, prefix + oldMid - 1);
        m_rows = std::move(nextRows);
        endRemoveRows();
        locker.unlock();
        emit visibleCountsChanged();
        return;
    }

    beginResetModel();
    m_rows = std::move(nextRows);
    endResetModel();
    locker.unlock();
    emit visibleCountsChanged();
}

bool MessageListModel::rowEquals(const Row &a, const Row &b)
{
    return a.type == b.type
            && a.hasTopGap == b.hasTopGap
            && a.bucketKey == b.bucketKey
            && a.title == b.title
            && a.expanded == b.expanded
            && a.stableKey == b.stableKey
            && a.message == b.message;
}

QList<int> MessageListModel::changedRoles(const Row &oldRow, const Row &newRow) const
{
    QSet<int> roleSet;
    roleSet.insert(RowTypeRole);
    roleSet.insert(IsHeaderRole);
    if (oldRow.hasTopGap != newRow.hasTopGap) roleSet.insert(HasTopGapRole);
    if (oldRow.bucketKey != newRow.bucketKey) roleSet.insert(BucketKeyRole);
    if (oldRow.title != newRow.title) roleSet.insert(TitleRole);
    if (oldRow.expanded != newRow.expanded) roleSet.insert(ExpandedRole);
    if (oldRow.stableKey != newRow.stableKey) roleSet.insert(MessageKeyRole);

    const QVariantMap &o = oldRow.message;
    const QVariantMap &n = newRow.message;
    if (o.value("accountEmail"_L1) != n.value("accountEmail"_L1)) roleSet.insert(AccountEmailRole);
    if (o.value("folder"_L1) != n.value("folder"_L1)) roleSet.insert(FolderRole);
    if (o.value("uid"_L1) != n.value("uid"_L1)) roleSet.insert(UidRole);
    if (o.value("sender"_L1) != n.value("sender"_L1)) roleSet.insert(SenderRole);
    if (o.value("subject"_L1) != n.value("subject"_L1)) roleSet.insert(SubjectRole);
    if (o.value("receivedAt"_L1) != n.value("receivedAt"_L1)) roleSet.insert(ReceivedAtRole);
    if (o.value("snippet"_L1) != n.value("snippet"_L1)) roleSet.insert(SnippetRole);
    if (o.value("unread"_L1)        != n.value("unread"_L1))        roleSet.insert(UnreadRole);
    if (o.value("recipient"_L1)     != n.value("recipient"_L1))     roleSet.insert(RecipientRole);
    if (o.value("avatarDomain"_L1)  != n.value("avatarDomain"_L1))  roleSet.insert(AvatarDomainRole);
    if (o.value("avatarUrl"_L1)     != n.value("avatarUrl"_L1))     roleSet.insert(AvatarUrlRole);
    if (o.value("avatarSource"_L1)  != n.value("avatarSource"_L1))  roleSet.insert(AvatarSourceRole);
    if (o.value("hasAttachments"_L1)   != n.value("hasAttachments"_L1))   roleSet.insert(HasAttachmentsRole);
    if (o.value("hasTrackingPixel"_L1) != n.value("hasTrackingPixel"_L1)) roleSet.insert(HasTrackingPixelRole);
    if (o.value("threadCount"_L1)   != n.value("threadCount"_L1))   roleSet.insert(ThreadCountRole);
    if (o.value("isImportant"_L1)   != n.value("isImportant"_L1))   roleSet.insert(IsImportantRole);
    if (o.value("allSenders"_L1)    != n.value("allSenders"_L1))    roleSet.insert(AllSendersRole);
    if (o.value("flagged"_L1)       != n.value("flagged"_L1))       roleSet.insert(FlaggedRole);

    QList<int> roles = roleSet.values();
    std::sort(roles.begin(), roles.end());
    return roles;
}

bool MessageListModel::bucketExpanded(const QString &bucketKey) const
{
    if (bucketKey == "today"_L1) return m_todayExpanded;
    if (bucketKey == "yesterday"_L1) return m_yesterdayExpanded;
    if (bucketKey.startsWith("weekday-"_L1)) return m_weekdayExpanded.value(bucketKey, true);
    if (bucketKey == "lastWeek"_L1) return m_lastWeekExpanded;
    if (bucketKey == "twoWeeksAgo"_L1) return m_twoWeeksAgoExpanded;
    return m_olderExpanded;
}
