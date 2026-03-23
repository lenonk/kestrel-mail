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
    if (!dt.isValid()) return QStringLiteral("older");
    const QDate target = dt.toLocalTime().date();
    const QDate today = QDate::currentDate();
    const int diffDays = target.daysTo(today);
    if (diffDays <= 0) return QStringLiteral("today");
    if (diffDays == 1) return QStringLiteral("yesterday");

    const QDate weekStart = today.addDays(-(today.dayOfWeek() % 7)); // Sunday-start week
    if (target >= weekStart && target < today) {
        return QStringLiteral("weekday-%1").arg(target.dayOfWeek());
    }

    if (diffDays <= 14) return QStringLiteral("lastWeek");
    if (diffDays <= 21) return QStringLiteral("twoWeeksAgo");
    return QStringLiteral("older");
}

QString bucketLabel(const QString &bucketKey)
{
    if (bucketKey == QStringLiteral("today")) return QStringLiteral("Today");
    if (bucketKey == QStringLiteral("yesterday")) return QStringLiteral("Yesterday");
    if (bucketKey.startsWith(QStringLiteral("weekday-"))) {
        bool ok = false;
        const int dow = bucketKey.mid(QStringLiteral("weekday-").size()).toInt(&ok);
        if (ok && dow >= 1 && dow <= 7) {
            return QLocale().dayName(dow, QLocale::LongFormat);
        }
    }
    if (bucketKey == QStringLiteral("lastWeek")) return QStringLiteral("Last Week");
    if (bucketKey == QStringLiteral("twoWeeksAgo")) return QStringLiteral("Two Weeks Ago");
    return QStringLiteral("Older");
}

QString messageStableKey(const QVariantMap &row)
{
    const QString account = row.value(QStringLiteral("accountEmail")).toString();
    const QString folder = row.value(QStringLiteral("folder")).toString();
    const QString uid = row.value(QStringLiteral("uid")).toString();
    if (!account.isEmpty() && !folder.isEmpty() && !uid.isEmpty()) {
        return QStringLiteral("msg:%1|%2|%3").arg(account, folder, uid);
    }
    return QStringLiteral("msg:%1|%2|%3|%4|%5")
            .arg(account,
                 row.value(QStringLiteral("uid")).toString(),
                 row.value(QStringLiteral("receivedAt")).toString(),
                 row.value(QStringLiteral("sender")).toString(),
                 row.value(QStringLiteral("subject")).toString());
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
    return m_rows.size();
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
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
    case AccountEmailRole: return row.message.value(QStringLiteral("accountEmail"));
    case FolderRole: return row.message.value(QStringLiteral("folder"));
    case UidRole: return row.message.value(QStringLiteral("uid"));
    case SenderRole: return row.message.value(QStringLiteral("sender"));
    case RecipientRole: return row.message.value(QStringLiteral("recipient"));
    case SubjectRole: return row.message.value(QStringLiteral("subject"));
    case ReceivedAtRole: return row.message.value(QStringLiteral("receivedAt"));
    case SnippetRole: return row.message.value(QStringLiteral("snippet"));
    case AvatarDomainRole: return row.message.value(QStringLiteral("avatarDomain"));
    case AvatarUrlRole: return row.message.value(QStringLiteral("avatarUrl"));
    case AvatarSourceRole: return row.message.value(QStringLiteral("avatarSource"));
    case UnreadRole: return row.message.value(QStringLiteral("unread"));
    case HasAttachmentsRole: return row.message.value(QStringLiteral("hasAttachments"));
    case HasTrackingPixelRole: return row.message.value(QStringLiteral("hasTrackingPixel"));
    case ThreadCountRole: return row.message.value(QStringLiteral("threadCount"));
    case IsImportantRole: return row.message.value(QStringLiteral("isImportant"));
    case AllSendersRole:  return row.message.value(QStringLiteral("allSenders"));
    case FlaggedRole:     return row.message.value(QStringLiteral("flagged"));
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
        { FlaggedRole,     QByteArrayLiteral("flagged") }
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
    refresh();
}

void MessageListModel::setBucketExpanded(const QString &bucketKey, bool expanded)
{
    // Expansion state only affects the view — no DB re-query needed.
    const QString key = bucketKey.trimmed();
    bool changed = false;
    if (key == QStringLiteral("today")) {
        changed = (m_todayExpanded != expanded);
        m_todayExpanded = expanded;
    } else if (key == QStringLiteral("yesterday")) {
        changed = (m_yesterdayExpanded != expanded);
        m_yesterdayExpanded = expanded;
    } else if (key == QStringLiteral("lastWeek")) {
        changed = (m_lastWeekExpanded != expanded);
        m_lastWeekExpanded = expanded;
    } else if (key == QStringLiteral("twoWeeksAgo")) {
        changed = (m_twoWeeksAgoExpanded != expanded);
        m_twoWeeksAgoExpanded = expanded;
    } else if (key == QStringLiteral("older")) {
        changed = (m_olderExpanded != expanded);
        m_olderExpanded = expanded;
    } else if (key.startsWith(QStringLiteral("weekday-"))) {
        const bool prev = m_weekdayExpanded.value(key, true);
        changed = (prev != expanded);
        m_weekdayExpanded.insert(key, expanded);
    }
    if (!changed) return;
    refreshView();
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

    watcher->setFuture(QtConcurrent::run(
        [store, folderKey, selectedCategories, selectedCategoryIndex, preserveLimit]() -> Result {
            QElapsedTimer t; t.start();
            bool hasMore = false;
            auto rows = store->messagesForSelection(
                folderKey, selectedCategories, selectedCategoryIndex, preserveLimit, 0, &hasMore);
            return {std::move(rows), hasMore};
        }));
}

void MessageListModel::refreshView()
{
    m_allRows = buildRows(m_loadedSourceRows);
    emit totalRowCountChanged();
    applyWindow();
}

void MessageListModel::applyWindow()
{
    if (m_windowStart < 0) m_windowStart = 0;

    const int effectiveWindowSize = (m_windowSize <= 0) ? m_allRows.size() : m_windowSize;
    if (m_windowStart >= m_allRows.size()) m_windowStart = qMax(0, m_allRows.size() - effectiveWindowSize);

    QVector<Row> window;
    const int end = qMin(m_allRows.size(), m_windowStart + effectiveWindowSize);
    for (int i = m_windowStart; i < end; ++i) window.push_back(m_allRows.at(i));
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

    if (!nextRows.isEmpty()) {
        const QVector<Row> deltaRows = buildRows(nextRows);
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
    emit totalRowCountChanged();

    if (m_windowStart < 0) m_windowStart = 0;
    const int effectiveWindowSize = (m_windowSize <= 0) ? m_allRows.size() : m_windowSize;
    if (m_windowStart >= m_allRows.size()) m_windowStart = qMax(0, m_allRows.size() - effectiveWindowSize);

    QVector<Row> window;
    const int end = qMin(m_allRows.size(), m_windowStart + effectiveWindowSize);
    for (int i = m_windowStart; i < end; ++i) window.push_back(m_allRows.at(i));
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
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &row = m_rows[i];
        if (row.type != MessageRow) continue;
        if (row.message.value(QStringLiteral("accountEmail")).toString() != accountEmail) continue;
        if (row.message.value(QStringLiteral("uid")).toString() != uid) continue;
        if (row.message.value(QStringLiteral("unread")).toInt() == 0) continue;
        row.message.insert(QStringLiteral("unread"), 0);
        emit dataChanged(index(i), index(i), {UnreadRole});
        break;
    }
    // Also patch m_allRows so the window doesn't restore the stale value on the next scroll.
    for (Row &row : m_allRows) {
        if (row.type == MessageRow
                && row.message.value(QStringLiteral("accountEmail")).toString() == accountEmail
                && row.message.value(QStringLiteral("uid")).toString() == uid) {
            row.message.insert(QStringLiteral("unread"), 0);
            break;
        }
    }
}

void MessageListModel::onMessageFlaggedChanged(const QString &accountEmail, const QString &uid, bool flagged)
{
    const int newFlagged = flagged ? 1 : 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &row = m_rows[i];
        if (row.type != MessageRow) continue;
        if (row.message.value(QStringLiteral("accountEmail")).toString() != accountEmail) continue;
        if (row.message.value(QStringLiteral("uid")).toString() != uid) continue;
        if (row.message.value(QStringLiteral("flagged")).toInt() == newFlagged) continue;
        row.message.insert(QStringLiteral("flagged"), newFlagged);
        emit dataChanged(index(i), index(i), {FlaggedRole});
        break;
    }
    for (Row &row : m_allRows) {
        if (row.type == MessageRow
                && row.message.value(QStringLiteral("accountEmail")).toString() == accountEmail
                && row.message.value(QStringLiteral("uid")).toString() == uid) {
            row.message.insert(QStringLiteral("flagged"), newFlagged);
            break;
        }
    }
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
    if (m_allRows.isEmpty()) return;
    if (m_windowSize <= 0) return; // unlimited mode
    const int step = qMax(10, m_windowSize / 2);
    const int maxStart = qMax(0, m_allRows.size() - m_windowSize);
    const int next = qMin(maxStart, m_windowStart + step);
    if (next == m_windowStart) return;
    m_windowStart = next;
    applyWindow();
}

void MessageListModel::shiftWindowUp()
{
    if (m_allRows.isEmpty()) return;
    if (m_windowSize <= 0) return; // unlimited mode
    const int step = qMax(10, m_windowSize / 2);
    const int next = qMax(0, m_windowStart - step);
    if (next == m_windowStart) return;
    m_windowStart = next;
    applyWindow();
}

QVector<MessageListModel::Row> MessageListModel::buildRows(const QVariantList &rows) const
{
    QVector<Row> out;
    QVariantList workingRows = rows;

    if (workingRows.isEmpty()) {
        const bool inboxSelected = m_folderKey.compare(QStringLiteral("account:inbox"), Qt::CaseInsensitive) == 0;
        const bool categoryTabActive = !m_selectedCategories.isEmpty();
        if (inboxSelected && !categoryTabActive) {
            QVariantMap mock;
            mock.insert(QStringLiteral("accountEmail"), QString());
            mock.insert(QStringLiteral("folder"), QStringLiteral("INBOX"));
            mock.insert(QStringLiteral("uid"), QStringLiteral("mock"));
            mock.insert(QStringLiteral("sender"), QStringLiteral("welcome@kestrel.mail"));
            mock.insert(QStringLiteral("subject"), QStringLiteral("Welcome to Kestrel Mail"));
            mock.insert(QStringLiteral("snippet"), QStringLiteral("Your account is set up. Press Refresh to load real mail from your provider."));
            mock.insert(QStringLiteral("receivedAt"), QStringLiteral("2026-02-17T11:00:00"));
            mock.insert(QStringLiteral("unread"), true);
            workingRows.push_back(mock);
        }
    }

    QHash<QString, QList<QVariantMap>> buckets;
    buckets.insert(QStringLiteral("today"), {});
    buckets.insert(QStringLiteral("yesterday"), {});
    buckets.insert(QStringLiteral("lastWeek"), {});
    buckets.insert(QStringLiteral("twoWeeksAgo"), {});
    buckets.insert(QStringLiteral("older"), {});

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
        const QString bucket = bucketKeyForDate(map.value(QStringLiteral("receivedAt")).toString());
        buckets[bucket].push_back(map);
    }

    QStringList order;
    order << QStringLiteral("today") << QStringLiteral("yesterday");
    order << weekdayOrder;
    order << QStringLiteral("lastWeek") << QStringLiteral("twoWeeksAgo") << QStringLiteral("older");

    for (const QString &bucket : order) {
        const QList<QVariantMap> rowsInBucket = buckets.value(bucket);
        if (rowsInBucket.isEmpty()) continue;

        Row header;
        header.type = HeaderRow;
        header.bucketKey = bucket;
        header.title = bucketLabel(bucket);
        header.expanded = bucketExpanded(bucket);
        header.hasTopGap = !out.isEmpty();
        header.stableKey = QStringLiteral("header:%1").arg(bucket);
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

int MessageListModel::visibleMessageCount() const
{
    int count = 0;
    for (const Row &r : m_rows) {
        if (r.type == MessageRow) ++count;
    }
    return count;
}

QVariantMap MessageListModel::rowAt(int index) const
{
    if (index < 0 || index >= m_rows.size()) return {};
    const Row &r = m_rows.at(index);
    QVariantMap result;
    result.insert("isHeader"_L1, r.type == HeaderRow);
    result.insert("messageKey"_L1, r.stableKey);
    return result;
}

void MessageListModel::applyRows(QVector<Row> &&nextRows)
{
    if (m_rows.isEmpty() && nextRows.isEmpty()) return;

    if (m_rows.isEmpty()) {
        beginInsertRows(QModelIndex(), 0, nextRows.size() - 1);
        m_rows = std::move(nextRows);
        endInsertRows();
        emit visibleCountsChanged();
        return;
    }

    if (nextRows.isEmpty()) {
        beginRemoveRows(QModelIndex(), 0, m_rows.size() - 1);
        m_rows.clear();
        endRemoveRows();
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

    if (oldMid == 0 && newMid > 0) {
        beginInsertRows(QModelIndex(), prefix, prefix + newMid - 1);
        m_rows = std::move(nextRows);
        endInsertRows();
        emit visibleCountsChanged();
        return;
    }

    if (newMid == 0 && oldMid > 0) {
        beginRemoveRows(QModelIndex(), prefix, prefix + oldMid - 1);
        m_rows = std::move(nextRows);
        endRemoveRows();
        emit visibleCountsChanged();
        return;
    }

    beginResetModel();
    m_rows = std::move(nextRows);
    endResetModel();
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
    if (o.value(QStringLiteral("accountEmail")) != n.value(QStringLiteral("accountEmail"))) roleSet.insert(AccountEmailRole);
    if (o.value(QStringLiteral("folder")) != n.value(QStringLiteral("folder"))) roleSet.insert(FolderRole);
    if (o.value(QStringLiteral("uid")) != n.value(QStringLiteral("uid"))) roleSet.insert(UidRole);
    if (o.value(QStringLiteral("sender")) != n.value(QStringLiteral("sender"))) roleSet.insert(SenderRole);
    if (o.value(QStringLiteral("subject")) != n.value(QStringLiteral("subject"))) roleSet.insert(SubjectRole);
    if (o.value(QStringLiteral("receivedAt")) != n.value(QStringLiteral("receivedAt"))) roleSet.insert(ReceivedAtRole);
    if (o.value(QStringLiteral("snippet")) != n.value(QStringLiteral("snippet"))) roleSet.insert(SnippetRole);
    if (o.value(QStringLiteral("unread"))        != n.value(QStringLiteral("unread")))        roleSet.insert(UnreadRole);
    if (o.value(QStringLiteral("recipient"))     != n.value(QStringLiteral("recipient")))     roleSet.insert(RecipientRole);
    if (o.value(QStringLiteral("avatarDomain"))  != n.value(QStringLiteral("avatarDomain")))  roleSet.insert(AvatarDomainRole);
    if (o.value(QStringLiteral("avatarUrl"))     != n.value(QStringLiteral("avatarUrl")))     roleSet.insert(AvatarUrlRole);
    if (o.value(QStringLiteral("avatarSource"))  != n.value(QStringLiteral("avatarSource")))  roleSet.insert(AvatarSourceRole);
    if (o.value(QStringLiteral("hasAttachments"))   != n.value(QStringLiteral("hasAttachments")))   roleSet.insert(HasAttachmentsRole);
    if (o.value(QStringLiteral("hasTrackingPixel")) != n.value(QStringLiteral("hasTrackingPixel"))) roleSet.insert(HasTrackingPixelRole);
    if (o.value(QStringLiteral("threadCount"))   != n.value(QStringLiteral("threadCount")))   roleSet.insert(ThreadCountRole);
    if (o.value(QStringLiteral("isImportant"))   != n.value(QStringLiteral("isImportant")))   roleSet.insert(IsImportantRole);
    if (o.value(QStringLiteral("allSenders"))    != n.value(QStringLiteral("allSenders")))    roleSet.insert(AllSendersRole);
    if (o.value(QStringLiteral("flagged"))       != n.value(QStringLiteral("flagged")))       roleSet.insert(FlaggedRole);

    QList<int> roles = roleSet.values();
    std::sort(roles.begin(), roles.end());
    return roles;
}

bool MessageListModel::bucketExpanded(const QString &bucketKey) const
{
    if (bucketKey == QStringLiteral("today")) return m_todayExpanded;
    if (bucketKey == QStringLiteral("yesterday")) return m_yesterdayExpanded;
    if (bucketKey.startsWith(QStringLiteral("weekday-"))) return m_weekdayExpanded.value(bucketKey, true);
    if (bucketKey == QStringLiteral("lastWeek")) return m_lastWeekExpanded;
    if (bucketKey == QStringLiteral("twoWeeksAgo")) return m_twoWeeksAgoExpanded;
    return m_olderExpanded;
}
