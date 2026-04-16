#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QFutureWatcher>
#include <QMutex>

class QTimer;
class DataStore;

class MessageListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(DataStore* dataStore READ dataStore WRITE setDataStore NOTIFY dataStoreChanged)
public:
    Q_PROPERTY(int totalRowCount READ totalRowCount NOTIFY totalRowCountChanged)
    Q_PROPERTY(int visibleRowCount READ visibleRowCount NOTIFY visibleCountsChanged)
    Q_PROPERTY(int visibleMessageCount READ visibleMessageCount NOTIFY visibleCountsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
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
    Q_INVOKABLE QVariantMap rowAt(int index) const;

    int totalRowCount() const;
    int visibleRowCount() const;
    int visibleMessageCount() const;
    bool loading() const { return m_loading; }
    QString searchQuery() const { return m_searchQuery; }
    bool isSearchActive() const { return !m_searchQuery.isEmpty(); }

signals:
    void dataStoreChanged();
    void totalRowCountChanged();
    void visibleCountsChanged();
    void loadingChanged();
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
    mutable QRecursiveMutex m_rowsMutex;
    QVector<Row> m_rows;
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
    QVariantList m_loadedSourceRows;
    quint64 m_refreshGeneration = 0;
    bool m_loading = false;
    int m_batchOffset = 0;
    QFutureWatcher<QVariantList> *m_refreshWatcher = nullptr;

    void loadNextBatch(quint64 generation);

    void scheduleRefresh();
    void onMessageMarkedRead(const QString &accountEmail, const QString &uid);
    void onMessageFlaggedChanged(const QString &accountEmail, const QString &uid, bool flagged);
    void refreshView();
    void appendRows(const QVariantList &batch);
    QVector<Row> buildRows(const QVariantList &rows) const;
    void applyRows(QVector<Row> &&nextRows);
    static bool rowEquals(const Row &a, const Row &b);
    QList<int> changedRoles(const Row &oldRow, const Row &newRow) ;
    bool bucketExpanded(const QString &bucketKey) const;
};
