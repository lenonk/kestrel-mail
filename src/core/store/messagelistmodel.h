#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QFutureWatcher>

class QTimer;
class DataStore;

class MessageListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(DataStore* dataStore READ dataStore WRITE setDataStore NOTIFY dataStoreChanged)
public:
    Q_PROPERTY(int windowSize READ windowSize WRITE setWindowSize NOTIFY windowSizeChanged)
    Q_PROPERTY(int totalRowCount READ totalRowCount NOTIFY totalRowCountChanged)
    Q_PROPERTY(int visibleRowCount READ visibleRowCount NOTIFY visibleCountsChanged)
    Q_PROPERTY(int visibleMessageCount READ visibleMessageCount NOTIFY visibleCountsChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY pagingChanged)
    Q_PROPERTY(int pageSize READ pageSize CONSTANT)
    Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(bool isSearchActive READ isSearchActive NOTIFY searchQueryChanged)

    enum RowType {
        HeaderRow = 0,
        MessageRow = 1
    };
    Q_ENUM(RowType)

    enum Roles {
        RowTypeRole = Qt::UserRole + 1,
        IsHeaderRole,
        HasTopGapRole,
        BucketKeyRole,
        TitleRole,
        ExpandedRole,
        MessageKeyRole,
        AccountEmailRole,
        FolderRole,
        UidRole,
        SenderRole,
        RecipientRole,
        SubjectRole,
        ReceivedAtRole,
        SnippetRole,
        AvatarDomainRole,
        AvatarUrlRole,
        AvatarSourceRole,
        UnreadRole,
        HasAttachmentsRole,
        HasTrackingPixelRole,
        ThreadCountRole,
        IsImportantRole,
        AllSendersRole,
        FlaggedRole,
        IsSearchResultRole,
        ResultFolderRole
    };

    explicit MessageListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    DataStore *dataStore() const;
    void setDataStore(DataStore *store);

    Q_INVOKABLE void setSelection(const QString &folderKey, const QStringList &selectedCategories, int selectedCategoryIndex);
    Q_INVOKABLE void setSearchQuery(const QString &query);
    Q_INVOKABLE void setBucketExpanded(const QString &bucketKey, bool expanded);
    Q_INVOKABLE void setExpansionState(bool todayExpanded,
                                       bool yesterdayExpanded,
                                       bool lastWeekExpanded,
                                       bool twoWeeksAgoExpanded,
                                       bool olderExpanded);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void shiftWindowDown();
    Q_INVOKABLE void shiftWindowUp();
    Q_INVOKABLE QVariantMap rowAt(int index) const;

    int windowSize() const { return m_windowSize; }
    Q_INVOKABLE void setWindowSize(int size);
    int totalRowCount() const { return m_allRows.size(); }
    int visibleRowCount() const { return m_rows.size(); }
    int visibleMessageCount() const;
    bool hasMore() const { return m_hasMore; }
    int pageSize() const { return m_pageSize; }
    QString searchQuery() const { return m_searchQuery; }
    bool isSearchActive() const { return !m_searchQuery.isEmpty(); }

signals:
    void dataStoreChanged();
    void windowSizeChanged();
    void totalRowCountChanged();
    void visibleCountsChanged();
    void pagingChanged();
    void searchQueryChanged();

private:
    struct Row {
        RowType type = MessageRow;
        bool hasTopGap = false;
        QString bucketKey;
        QString title;
        bool expanded = false;
        QString stableKey;
        QVariantMap message;
    };

    QPointer<DataStore> m_dataStore;
    QVector<Row> m_rows;
    QVector<Row> m_allRows;
    QString m_folderKey;
    QString m_searchQuery;
    QStringList m_selectedCategories;
    int m_selectedCategoryIndex = 0;
    bool m_todayExpanded = true;
    bool m_yesterdayExpanded = true;
    bool m_lastWeekExpanded = true;
    bool m_twoWeeksAgoExpanded = true;
    bool m_olderExpanded = true;
    QHash<QString, bool> m_weekdayExpanded;
    QTimer *m_reloadDebounceTimer = nullptr;
    int m_windowStart = 0;
    // 0 means "unlimited" (show full list at once).
    int m_windowSize = 0;
    QVariantList m_loadedSourceRows;
    int m_nextOffset = 0;
    const int m_pageSize = 50;
    bool m_hasMore = false;
    bool m_isLoadingPage = false;
    bool m_pendingRefresh = false;
    QFutureWatcher<std::pair<QVariantList, bool>> *m_refreshWatcher = nullptr;

    void scheduleRefresh();
    void onMessageMarkedRead(const QString &accountEmail, const QString &uid);
    void onMessageFlaggedChanged(const QString &accountEmail, const QString &uid, bool flagged);
    void refreshView();
    void applyWindow();
    QVector<Row> buildRows(const QVariantList &rows) const;
    void applyRows(QVector<Row> &&nextRows);
    static bool rowEquals(const Row &a, const Row &b);
    QList<int> changedRoles(const Row &oldRow, const Row &newRow) const;
    bool bucketExpanded(const QString &bucketKey) const;
};
