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

constexpr QLatin1StringView kBucketToday       {"today"};
constexpr QLatin1StringView kBucketYesterday   {"yesterday"};
constexpr QLatin1StringView kBucketWeekday     {"weekday-"};
constexpr QLatin1StringView kBucketLastWeek    {"lastWeek"};
constexpr QLatin1StringView kBucketTwoWeeksAgo {"twoWeeksAgo"};
constexpr QLatin1StringView kBucketOlder       {"older"};

enum MailAge : int8_t {
    Today       = 0,
    Yesterday   = 1,
    ThisWeek    = 7,
    LastWeek    = 14,
    TwoWeeksAgo = 21,
};

QString
bucketKeyForDate(const QString &dateValue) {
    const auto dt = QDateTime::fromString(dateValue, Qt::ISODate);
    if (!dt.isValid()) { return QString(kBucketOlder); }

    const auto target = dt.toLocalTime().date();
    const auto today = QDate::currentDate();
    const auto diffDays = target.daysTo(today);

    if (diffDays <= MailAge::Today) { return QString(kBucketToday); }
    if (diffDays == MailAge::Yesterday) { return QString(kBucketYesterday); }

    if (const auto weekStart = today.addDays(-(today.dayOfWeek() % MailAge::ThisWeek)); target >= weekStart && target < today) {
        return QStringLiteral("weekday-%1").arg(target.dayOfWeek());
    }

    if (diffDays <= MailAge::LastWeek) { return QString(kBucketLastWeek); }
    if (diffDays <= MailAge::TwoWeeksAgo) { return QString(kBucketTwoWeeksAgo); }

    return QString(kBucketOlder);
}

QString
bucketLabel(const QString &bucketKey) {
    if (bucketKey == kBucketToday) { return "Today"_L1; }
    if (bucketKey == kBucketYesterday) { return "Yesterday"_L1; }

    if (bucketKey.startsWith(kBucketWeekday)) {
        bool ok = false;
        const auto dow = QStringView{bucketKey}.mid(kBucketWeekday.size()).toInt(&ok);

        if (ok && dow >= MailAge::Today && dow <= MailAge::ThisWeek) {
            return QLocale().dayName(dow, QLocale::LongFormat);
        }
    }

    if (bucketKey == kBucketLastWeek) { return "Last Week"_L1; }
    if (bucketKey == kBucketTwoWeeksAgo) { return "Two Weeks Ago"_L1; }

    return "Older"_L1;
}

QString
messageStableKey(const QVariantMap &row) {
    const auto account = row.value("accountEmail"_L1).toString();
    const auto folder = row.value("folder"_L1).toString();

    if (const auto uid = row.value("uid"_L1).toString(); !account.isEmpty() && !folder.isEmpty() && !uid.isEmpty()) {
        return "msg:%1|%2|%3"_L1.arg(account, folder, uid);
    }

    return "msg:%1|%2|%3|%4|%5"_L1.arg(account, row.value("uid"_L1).toString(), row.value("receivedAt"_L1).toString(),
        row.value("sender"_L1).toString(), row.value("subject"_L1).toString());
}

} // namespace

MessageListModel::MessageListModel(QObject *parent)
    : QAbstractListModel(parent) {
    m_reloadDebounceTimer = new QTimer(this);
    m_reloadDebounceTimer->setSingleShot(true);
    m_reloadDebounceTimer->setInterval(80);
    connect(m_reloadDebounceTimer, &QTimer::timeout, this, &MessageListModel::refresh);
}

qint32
MessageListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) { return 0; }

    QMutexLocker locker(&m_rowsMutex);

    return static_cast<qint32>(m_rows.size());
}

QVariant
MessageListModel::data(const QModelIndex &index, const int role) const {
    QMutexLocker locker(&m_rowsMutex);

    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) { return {}; }

    const auto &[type, hasTopGap, bucketKey, title, expanded, stableKey, message] = m_rows.at(index.row());

    switch (role) {
        case RowTypeRole:           return type;
        case IsHeaderRole:          return type == HeaderRow;
        case HasTopGapRole:         return hasTopGap;
        case BucketKeyRole:         return bucketKey;
        case TitleRole:             return title;
        case ExpandedRole:          return expanded;
        case MessageKeyRole:        return stableKey;
        case AccountEmailRole:      return message.value("accountEmail"_L1);
        case FolderRole:            return message.value("folder"_L1);
        case UidRole:               return message.value("uid"_L1);
        case SenderRole:            return message.value("sender"_L1);
        case RecipientRole:         return message.value("recipient"_L1);
        case SubjectRole:           return message.value("subject"_L1);
        case ReceivedAtRole:        return message.value("receivedAt"_L1);
        case SnippetRole:           return message.value("snippet"_L1);
        case AvatarDomainRole:      return message.value("avatarDomain"_L1);
        case AvatarUrlRole:         return message.value("avatarUrl"_L1);
        case AvatarSourceRole:      return message.value("avatarSource"_L1);
        case UnreadRole:            return message.value("unread"_L1);
        case HasAttachmentsRole:    return message.value("hasAttachments"_L1);
        case HasTrackingPixelRole:  return message.value("hasTrackingPixel"_L1);
        case ThreadCountRole:       return message.value("threadCount"_L1);
        case IsImportantRole:       return message.value("isImportant"_L1);
        case AllSendersRole:        return message.value("allSenders"_L1);
        case FlaggedRole:           return message.value("flagged"_L1);
        case IsSearchResultRole:    return message.value("isSearchResult"_L1);
        case ResultFolderRole:      return message.value("folder"_L1);
        default:                    return {};
    }
}

QHash<int, QByteArray>
MessageListModel::roleNames() const {
    return {
        { RowTypeRole,          QByteArrayLiteral("rowType") },
        { IsHeaderRole,         QByteArrayLiteral("isHeader") },
        { HasTopGapRole,        QByteArrayLiteral("hasTopGap") },
        { BucketKeyRole,        QByteArrayLiteral("bucketKey") },
        { TitleRole,            QByteArrayLiteral("title") },
        { ExpandedRole,         QByteArrayLiteral("expanded") },
        { MessageKeyRole,       QByteArrayLiteral("messageKey") },
        { AccountEmailRole,     QByteArrayLiteral("accountEmail") },
        { FolderRole,           QByteArrayLiteral("folder") },
        { UidRole,              QByteArrayLiteral("uid") },
        { SenderRole,           QByteArrayLiteral("sender") },
        { RecipientRole,        QByteArrayLiteral("recipient") },
        { SubjectRole,          QByteArrayLiteral("subject") },
        { ReceivedAtRole,       QByteArrayLiteral("receivedAt") },
        { SnippetRole,          QByteArrayLiteral("snippet") },
        { AvatarDomainRole,     QByteArrayLiteral("avatarDomain") },
        { AvatarUrlRole,        QByteArrayLiteral("avatarUrl") },
        { AvatarSourceRole,     QByteArrayLiteral("avatarSource") },
        { UnreadRole,           QByteArrayLiteral("unread") },
        { HasAttachmentsRole,   QByteArrayLiteral("hasAttachments") },
        { HasTrackingPixelRole, QByteArrayLiteral("hasTrackingPixel") },
        { ThreadCountRole,      QByteArrayLiteral("threadCount") },
        { IsImportantRole,      QByteArrayLiteral("isImportant") },
        { AllSendersRole,       QByteArrayLiteral("allSenders") },
        { FlaggedRole,          QByteArrayLiteral("flagged") },
        { IsSearchResultRole,   QByteArrayLiteral("isSearchResult") },
        { ResultFolderRole,     QByteArrayLiteral("resultFolder") }
    };
}

DataStore *
MessageListModel::dataStore() const { return m_dataStore.data(); }

void
MessageListModel::setDataStore(DataStore *store) {
    if (m_dataStore == store) { return; }

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

void
MessageListModel::setSelection(const QString &folderKey, const QStringList &selectedCategories, const int selectedCategoryIndex) {
    const int normalizedIndex = qMax(0, selectedCategoryIndex);

    if (m_folderKey == folderKey && m_selectedCategories == selectedCategories && m_selectedCategoryIndex == normalizedIndex) {
        return;
    }

    m_folderKey = folderKey;
    m_selectedCategories = selectedCategories;
    m_selectedCategoryIndex = normalizedIndex;
    m_loadedSourceRows.clear();

    if (!m_searchQuery.isEmpty()) return; // search takes priority over folder selection
    refresh();
}

void
MessageListModel::setSearchQuery(const QString &query) {
    const auto trimmed = query.trimmed();

    if (m_searchQuery == trimmed) { return; }

    m_searchQuery = trimmed;
    m_loadedSourceRows.clear();

    emit searchQueryChanged();

    refresh();
}

void
MessageListModel::setBucketExpanded(const QString &bucketKey, const bool expanded) {
    // Expansion state only affects the view — no DB re-query needed.
    const auto key = bucketKey.trimmed();
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
    } else if (key.startsWith(kBucketWeekday)) {
        const bool prev = m_weekdayExpanded.value(key, true);
        changed = (prev != expanded);
        m_weekdayExpanded.insert(key, expanded);
    }

    if (!changed) return;
    refreshView();
}

void
MessageListModel::setExpansionState(const bool todayExpanded,
                                         const bool yesterdayExpanded,
                                         const bool lastWeekExpanded,
                                         const bool twoWeeksAgoExpanded,
                                         const bool olderExpanded) {
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

void
MessageListModel::refresh() {
    const auto generation = ++m_refreshGeneration;

    DataStore *store = m_dataStore.data();

    if (!store) {
        m_loadedSourceRows = {};
        if (m_loading) { m_loading = false; emit loadingChanged(); }
        refreshView();
        return;
    }

    // Reset for a fresh load and show the first batch immediately.
    m_loadedSourceRows.clear();
    m_batchOffset = 0;

    loadNextBatch(generation);
}

void
MessageListModel::loadNextBatch(const quint64 generation) {
    static constexpr int kBatchSize = 5000;

    DataStore *store = m_dataStore.data();
    if (!store || generation != m_refreshGeneration) {
        if (m_loading) { m_loading = false; emit loadingChanged(); }
        return;
    }

    const auto folderKey = m_folderKey;
    const auto selectedCategories = m_selectedCategories;
    const auto selectedCategoryIndex = m_selectedCategoryIndex;
    const auto searchQuery = m_searchQuery;
    const auto offset = m_batchOffset;

    auto *watcher = new QFutureWatcher<QVariantList>(this);
    m_refreshWatcher = watcher;

    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher, generation]() {
        m_refreshWatcher = nullptr;
        watcher->deleteLater();

        if (generation != m_refreshGeneration)
            return;

        const auto batch = watcher->result();

        if (!batch.isEmpty()) {
            m_loadedSourceRows.reserve(m_loadedSourceRows.size() + batch.size());
            for (const auto &v : batch)
                m_loadedSourceRows.push_back(v);
            m_batchOffset = m_loadedSourceRows.size();

            // First batch: full rebuild. Subsequent batches: append only.
            if (m_batchOffset <= kBatchSize)
                refreshView();
            else
                appendRows(batch);
        }

        if (batch.size() < kBatchSize) {
            // No more data — done loading.
            if (m_loading) { m_loading = false; emit loadingChanged(); }
        } else {
            // More data available — show progress bar and fetch next batch.
            if (!m_loading) { m_loading = true; emit loadingChanged(); }
            loadNextBatch(generation);
        }
    });

    watcher->setFuture(QtConcurrent::run(
        [store, folderKey, selectedCategories, selectedCategoryIndex, searchQuery, offset]() -> QVariantList {
            bool hasMore = false;
            if (!searchQuery.isEmpty())
                return store->searchMessages(searchQuery, kBatchSize, offset, &hasMore);
            return store->messagesForSelection(
                folderKey, selectedCategories, selectedCategoryIndex, kBatchSize, offset, &hasMore);
        }));
}

void
MessageListModel::refreshView() {
    QVector<Row> built = buildRows(m_loadedSourceRows);
    emit totalRowCountChanged();
    applyRows(std::move(built));
}

void
MessageListModel::appendRows(const QVariantList &batch) {
    QVector<Row> delta = buildRows(batch);
    if (delta.isEmpty()) return;

    // Collect existing stable keys and bucket headers to deduplicate.
    QSet<QString> existingKeys;
    {
        QMutexLocker locker(&m_rowsMutex);
        for (const auto &r : m_rows)
            existingKeys.insert(r.stableKey);
    }

    auto pending = std::make_shared<QVector<Row>>();
    for (auto &r : delta) {
        if (existingKeys.contains(r.stableKey)) continue;
        if (r.type == HeaderRow)
            r.hasTopGap = true;
        existingKeys.insert(r.stableKey);
        pending->push_back(std::move(r));
    }

    if (pending->isEmpty()) return;

    // Drip-feed rows in small chunks so the UI stays responsive during scrolling.
    auto offset = std::make_shared<int>(0);
    auto generation = m_refreshGeneration;
    auto drip = std::make_shared<std::function<void()>>();
    *drip = [this, pending, offset, generation, drip]() {
        static constexpr int kChunk = 200;
        if (generation != m_refreshGeneration) return;
        if (*offset >= pending->size()) return;

        const auto count = qMin(kChunk, static_cast<int>(pending->size()) - *offset);

        QMutexLocker locker(&m_rowsMutex);
        const auto first = m_rows.size();
        const auto last = first + count - 1;

        beginInsertRows(QModelIndex(), first, last);
        for (int i = 0; i < count; ++i)
            m_rows.push_back(std::move((*pending)[*offset + i]));
        endInsertRows();
        locker.unlock();

        *offset += count;

        emit totalRowCountChanged();
        emit visibleCountsChanged();

        if (*offset < pending->size())
            QTimer::singleShot(0, this, [drip]() { (*drip)(); });
    };

    (*drip)();
}

void
MessageListModel::scheduleRefresh() {
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

void
MessageListModel::onMessageMarkedRead(const QString &accountEmail, const QString &uid) {
    // Directly update the unread role for any visible row matching this account+uid.
    // This bypasses the 80ms refresh throttle so the unread dot disappears immediately.
    // The subsequent full refresh (from inboxChanged) will reconcile m_loadedSourceRows.
    int changedIndex = -1;

    {
        QMutexLocker locker(&m_rowsMutex);
        for (int i = 0; i < m_rows.size(); ++i) {
            Row &row = m_rows[i];

            if (row.type != MessageRow) { continue; }
            if (row.message.value("accountEmail"_L1).toString() != accountEmail) { continue; }
            if (row.message.value("uid"_L1).toString() != uid) { continue; }
            if (row.message.value("unread"_L1).toInt() == 0) { continue; }

            row.message.insert("unread"_L1, 0);
            changedIndex = i;

            break;
        }
    }

    if (changedIndex >= 0) {
        emit dataChanged(index(changedIndex), index(changedIndex), {UnreadRole});
    }
}

void
MessageListModel::onMessageFlaggedChanged(const QString &accountEmail, const QString &uid, const bool flagged) {
    const auto newFlagged = flagged ? 1 : 0;
    int changedIndex = -1;

    {
        QMutexLocker locker(&m_rowsMutex);
        for (int i = 0; i < m_rows.size(); ++i) {
            Row &row = m_rows[i];

            if (row.type != MessageRow) { continue; }
            if (row.message.value("accountEmail"_L1).toString() != accountEmail) { continue; }
            if (row.message.value("uid"_L1).toString() != uid) { continue; }
            if (row.message.value("flagged"_L1).toInt() == newFlagged) { continue; }

            row.message.insert("flagged"_L1, newFlagged);
            changedIndex = i;

            break;
        }
    }

    if (changedIndex >= 0) {
        emit dataChanged(index(changedIndex), index(changedIndex), {FlaggedRole});
    }
}

QVector<MessageListModel::Row>
MessageListModel::buildRows(const QVariantList &rows) const {
    QVector<Row> out;
    QVariantList workingRows = rows;

    if (workingRows.isEmpty()) {
        const auto inboxSelected = m_folderKey.compare("account:inbox"_L1, Qt::CaseInsensitive) == 0;
        if (const auto categoryTabActive = !m_selectedCategories.isEmpty(); inboxSelected && !categoryTabActive) {
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

    const auto today = QDate::currentDate();
    const auto weekStart = today.addDays(-(today.dayOfWeek() % 7)); // Sunday-start week
    QStringList weekdayOrder;

    for (QDate d = today.addDays(-2); d >= weekStart; d = d.addDays(-1)) {
        if (const auto key = QStringLiteral("weekday-%1").arg(d.dayOfWeek()); !weekdayOrder.contains(key)) {
            weekdayOrder.push_back(key);
            buckets.insert(key, {});
        }
    }

    QSet<QString> dedupe;
    for (const auto &v : workingRows) {
        const auto map = v.toMap();
        const auto stable = messageStableKey(map);

        if (dedupe.contains(stable)) { continue; }

        dedupe.insert(stable);
        const auto bucket = bucketKeyForDate(map.value("receivedAt"_L1).toString());
        buckets[bucket].push_back(map);
    }

    QStringList order;
    order << "today"_L1 << "yesterday"_L1;
    order << weekdayOrder;
    order << "lastWeek"_L1 << "twoWeeksAgo"_L1 << "older"_L1;

    for (const auto &bucket : order) {
        const auto rowsInBucket = buckets.value(bucket);
        if (rowsInBucket.isEmpty()) { continue; }

        Row header;
        header.type = HeaderRow;
        header.bucketKey = bucket;
        header.title = bucketLabel(bucket);
        header.expanded = bucketExpanded(bucket);
        header.hasTopGap = !out.isEmpty();
        header.stableKey = "header:%1"_L1.arg(bucket);
        out.push_back(header);

        if (!header.expanded) continue;

        for (const auto &message : rowsInBucket) {
            Row row;
            row.type = MessageRow;
            row.stableKey = messageStableKey(message);
            row.message = message;
            out.push_back(row);
        }
    }

    return out;
}

int
MessageListModel::totalRowCount() const {
    QMutexLocker locker(&m_rowsMutex);
    return m_rows.size();
}

int
MessageListModel::visibleRowCount() const {
    QMutexLocker locker(&m_rowsMutex);
    return m_rows.size();
}

int
MessageListModel::visibleMessageCount() const {
    QMutexLocker locker(&m_rowsMutex);
    int count = 0;

    for (const Row &r : m_rows) {
        if (r.type == MessageRow) ++count;
    }

    return count;
}

QVariantMap
MessageListModel::rowAt(const int index) const {
    QMutexLocker locker(&m_rowsMutex);

    if (index < 0 || index >= m_rows.size()) { return {}; }

    const auto &r = m_rows.at(index);
    QVariantMap result;
    result.insert("isHeader"_L1, r.type == HeaderRow);
    result.insert("messageKey"_L1, r.stableKey);

    return result;
}

void
MessageListModel::applyRows(QVector<Row> &&nextRows) {
    QMutexLocker locker(&m_rowsMutex);

    if (m_rows.isEmpty() && nextRows.isEmpty()) { return; }

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

    const auto oldSize = m_rows.size();
    const auto newSize = nextRows.size();

    if (oldSize == newSize) {
        auto sameKeys = true;

        for (int i = 0; i < oldSize; ++i) {
            if (m_rows.at(i).stableKey != nextRows.at(i).stableKey) {
                sameKeys = false;
                break;
            }
        }

        if (sameKeys) {
            for (int i = 0; i < oldSize; ++i) {
                const auto &oldRow = m_rows.at(i);
                const auto &newRow = nextRows.at(i);
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

    const auto oldMid = oldSize - prefix - suffix;
    const auto newMid = newSize - prefix - suffix;

    // Update prefix rows in-place and emit dataChanged so QML sees the new
    // values (e.g. a header's expanded state) before the structural change.
    for (int i = 0; i < prefix; ++i) {
        if (!rowEquals(m_rows.at(i), nextRows.at(i))) {
            const auto roles = changedRoles(m_rows.at(i), nextRows.at(i));
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

bool
MessageListModel::rowEquals(const Row &a, const Row &b) {
    return a.type == b.type
            && a.hasTopGap == b.hasTopGap
            && a.bucketKey == b.bucketKey
            && a.title == b.title
            && a.expanded == b.expanded
            && a.stableKey == b.stableKey
            && a.message == b.message;
}

QList<int>
MessageListModel::changedRoles(const Row &oldRow, const Row &newRow) {
    QSet<int> roleSet;
    roleSet.insert(RowTypeRole);
    roleSet.insert(IsHeaderRole);

    if (oldRow.hasTopGap != newRow.hasTopGap) { roleSet.insert(HasTopGapRole); }
    if (oldRow.bucketKey != newRow.bucketKey) { roleSet.insert(BucketKeyRole); }
    if (oldRow.title != newRow.title) { roleSet.insert(TitleRole); }
    if (oldRow.expanded != newRow.expanded) { roleSet.insert(ExpandedRole); }
    if (oldRow.stableKey != newRow.stableKey) { roleSet.insert(MessageKeyRole); }

    const QVariantMap &o = oldRow.message;
    const QVariantMap &n = newRow.message;

    if (o.value("accountEmail"_L1)      != n.value("accountEmail"_L1))      { roleSet.insert(AccountEmailRole); }
    if (o.value("folder"_L1)            != n.value("folder"_L1))            { roleSet.insert(FolderRole); }
    if (o.value("uid"_L1)               != n.value("uid"_L1))               { roleSet.insert(UidRole); }
    if (o.value("sender"_L1)            != n.value("sender"_L1))            { roleSet.insert(SenderRole); }
    if (o.value("subject"_L1)           != n.value("subject"_L1))           { roleSet.insert(SubjectRole); }
    if (o.value("receivedAt"_L1)        != n.value("receivedAt"_L1))        { roleSet.insert(ReceivedAtRole); }
    if (o.value("snippet"_L1)           != n.value("snippet"_L1))           { roleSet.insert(SnippetRole); }
    if (o.value("unread"_L1)            != n.value("unread"_L1))            { roleSet.insert(UnreadRole); }
    if (o.value("recipient"_L1)         != n.value("recipient"_L1))         { roleSet.insert(RecipientRole); }
    if (o.value("avatarDomain"_L1)      != n.value("avatarDomain"_L1))      { roleSet.insert(AvatarDomainRole); }
    if (o.value("avatarUrl"_L1)         != n.value("avatarUrl"_L1))         { roleSet.insert(AvatarUrlRole); }
    if (o.value("avatarSource"_L1)      != n.value("avatarSource"_L1))      { roleSet.insert(AvatarSourceRole); }
    if (o.value("hasAttachments"_L1)    != n.value("hasAttachments"_L1))    { roleSet.insert(HasAttachmentsRole); }
    if (o.value("hasTrackingPixel"_L1)  != n.value("hasTrackingPixel"_L1))  { roleSet.insert(HasTrackingPixelRole); }
    if (o.value("threadCount"_L1)       != n.value("threadCount"_L1))       { roleSet.insert(ThreadCountRole); }
    if (o.value("isImportant"_L1)       != n.value("isImportant"_L1))       { roleSet.insert(IsImportantRole); }
    if (o.value("allSenders"_L1)        != n.value("allSenders"_L1))        { roleSet.insert(AllSendersRole); }
    if (o.value("flagged"_L1)           != n.value("flagged"_L1))           { roleSet.insert(FlaggedRole); }

    auto roles = roleSet.values();
    std::ranges::sort(roles);

    return roles;
}

bool
MessageListModel::bucketExpanded(const QString &bucketKey) const {
    if (bucketKey == "today"_L1) return m_todayExpanded;
    if (bucketKey == "yesterday"_L1) return m_yesterdayExpanded;
    if (bucketKey.startsWith(kBucketWeekday)) return m_weekdayExpanded.value(bucketKey, true);
    if (bucketKey == "lastWeek"_L1) return m_lastWeekExpanded;
    if (bucketKey == "twoWeeksAgo"_L1) return m_twoWeeksAgoExpanded;
    return m_olderExpanded;
}
