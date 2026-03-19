/****************************************************************************
** Meta object code from reading C++ file 'datastore.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/store/datastore.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'datastore.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN9DataStoreE_t {};
} // unnamed namespace

template <> constexpr inline auto DataStore::qt_create_metaobjectdata<qt_meta_tag_ZN9DataStoreE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "DataStore",
        "inboxChanged",
        "",
        "foldersChanged",
        "messageMarkedRead",
        "accountEmail",
        "uid",
        "bodyHtmlUpdated",
        "folder",
        "init",
        "upsertHeader",
        "QVariantMap",
        "header",
        "upsertHeaders",
        "QVariantList",
        "headers",
        "upsertFolder",
        "pruneFolderToUids",
        "uids",
        "removeAccountUidsEverywhere",
        "skipOrphanCleanup",
        "markMessageRead",
        "folderUids",
        "folderMaxUid",
        "folderMessageCount",
        "folderSyncStatus",
        "upsertFolderSyncStatus",
        "uidNext",
        "highestModSeq",
        "messages",
        "bodyFetchCandidates",
        "limit",
        "bodyFetchCandidatesByAccount",
        "fetchCandidatesForMessageKey",
        "messageByKey",
        "messagesForThread",
        "threadId",
        "hasUsableBodyForEdge",
        "updateBodyForKey",
        "bodyHtml",
        "reloadInbox",
        "reloadFolders",
        "messagesForSelection",
        "folderKey",
        "selectedCategories",
        "selectedCategoryIndex",
        "offset",
        "bool*",
        "hasMore",
        "groupedMessagesForSelection",
        "todayExpanded",
        "yesterdayExpanded",
        "lastWeekExpanded",
        "twoWeeksAgoExpanded",
        "olderExpanded",
        "statsForFolder",
        "rawFolderName",
        "hasCachedHeadersForFolder",
        "minCount",
        "inboxCategoryTabs",
        "tagItems",
        "avatarForEmail",
        "email",
        "displayNameForEmail",
        "preferredSelfDisplayName",
        "migrationStats",
        "avatarShouldRefresh",
        "ttlSeconds",
        "maxFailures",
        "staleGooglePeopleEmails",
        "updateContactAvatar",
        "avatarUrl",
        "source",
        "isSenderTrusted",
        "domain",
        "setTrustedSenderDomain",
        "attachmentsForMessage",
        "searchContacts",
        "prefix",
        "inbox",
        "folders"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'inboxChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'foldersChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'messageMarkedRead'
        QtMocHelpers::SignalData<void(const QString &, const QString &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 6 },
        }}),
        // Signal 'bodyHtmlUpdated'
        QtMocHelpers::SignalData<void(const QString &, const QString &, const QString &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 }, { QMetaType::QString, 6 },
        }}),
        // Method 'init'
        QtMocHelpers::MethodData<bool()>(9, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'upsertHeader'
        QtMocHelpers::MethodData<void(const QVariantMap &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 11, 12 },
        }}),
        // Method 'upsertHeaders'
        QtMocHelpers::MethodData<void(const QVariantList &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 14, 15 },
        }}),
        // Method 'upsertFolder'
        QtMocHelpers::MethodData<void(const QVariantMap &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 11, 8 },
        }}),
        // Method 'pruneFolderToUids'
        QtMocHelpers::MethodData<void(const QString &, const QString &, const QStringList &)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 }, { QMetaType::QStringList, 18 },
        }}),
        // Method 'removeAccountUidsEverywhere'
        QtMocHelpers::MethodData<void(const QString &, const QStringList &, bool)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QStringList, 18 }, { QMetaType::Bool, 20 },
        }}),
        // Method 'removeAccountUidsEverywhere'
        QtMocHelpers::MethodData<void(const QString &, const QStringList &)>(19, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QStringList, 18 },
        }}),
        // Method 'markMessageRead'
        QtMocHelpers::MethodData<void(const QString &, const QString &)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 6 },
        }}),
        // Method 'folderUids'
        QtMocHelpers::MethodData<QStringList(const QString &, const QString &) const>(22, 2, QMC::AccessPublic, QMetaType::QStringList, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 },
        }}),
        // Method 'folderMaxUid'
        QtMocHelpers::MethodData<qint64(const QString &, const QString &) const>(23, 2, QMC::AccessPublic, QMetaType::LongLong, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 },
        }}),
        // Method 'folderMessageCount'
        QtMocHelpers::MethodData<qint64(const QString &, const QString &) const>(24, 2, QMC::AccessPublic, QMetaType::LongLong, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 },
        }}),
        // Method 'folderSyncStatus'
        QtMocHelpers::MethodData<QVariantMap(const QString &, const QString &) const>(25, 2, QMC::AccessPublic, 0x80000000 | 11, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 },
        }}),
        // Method 'upsertFolderSyncStatus'
        QtMocHelpers::MethodData<void(const QString &, const QString &, qint64, qint64, qint64)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 }, { QMetaType::LongLong, 27 }, { QMetaType::LongLong, 28 },
            { QMetaType::LongLong, 29 },
        }}),
        // Method 'bodyFetchCandidates'
        QtMocHelpers::MethodData<QStringList(const QString &, const QString &, int) const>(30, 2, QMC::AccessPublic, QMetaType::QStringList, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 }, { QMetaType::Int, 31 },
        }}),
        // Method 'bodyFetchCandidates'
        QtMocHelpers::MethodData<QStringList(const QString &, const QString &) const>(30, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QStringList, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 },
        }}),
        // Method 'bodyFetchCandidatesByAccount'
        QtMocHelpers::MethodData<QVariantList(const QString &, int) const>(32, 2, QMC::AccessPublic, 0x80000000 | 14, {{
            { QMetaType::QString, 5 }, { QMetaType::Int, 31 },
        }}),
        // Method 'bodyFetchCandidatesByAccount'
        QtMocHelpers::MethodData<QVariantList(const QString &) const>(32, 2, QMC::AccessPublic | QMC::MethodCloned, 0x80000000 | 14, {{
            { QMetaType::QString, 5 },
        }}),
        // Method 'fetchCandidatesForMessageKey'
        QtMocHelpers::MethodData<QVariantList(const QString &, const QString &, const QString &) const>(33, 2, QMC::AccessPublic, 0x80000000 | 14, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 }, { QMetaType::QString, 6 },
        }}),
        // Method 'messageByKey'
        QtMocHelpers::MethodData<QVariantMap(const QString &, const QString &, const QString &) const>(34, 2, QMC::AccessPublic, 0x80000000 | 11, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 }, { QMetaType::QString, 6 },
        }}),
        // Method 'messagesForThread'
        QtMocHelpers::MethodData<QVariantList(const QString &, const QString &) const>(35, 2, QMC::AccessPublic, 0x80000000 | 14, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 36 },
        }}),
        // Method 'hasUsableBodyForEdge'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QString &) const>(37, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 }, { QMetaType::QString, 6 },
        }}),
        // Method 'updateBodyForKey'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, const QString &, const QString &)>(38, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 }, { QMetaType::QString, 6 }, { QMetaType::QString, 39 },
        }}),
        // Method 'reloadInbox'
        QtMocHelpers::MethodData<void()>(40, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'reloadFolders'
        QtMocHelpers::MethodData<void()>(41, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'messagesForSelection'
        QtMocHelpers::MethodData<QVariantList(const QString &, const QStringList &, int, int, int, bool *) const>(42, 2, QMC::AccessPublic, 0x80000000 | 14, {{
            { QMetaType::QString, 43 }, { QMetaType::QStringList, 44 }, { QMetaType::Int, 45 }, { QMetaType::Int, 31 },
            { QMetaType::Int, 46 }, { 0x80000000 | 47, 48 },
        }}),
        // Method 'messagesForSelection'
        QtMocHelpers::MethodData<QVariantList(const QString &, const QStringList &, int, int, int) const>(42, 2, QMC::AccessPublic | QMC::MethodCloned, 0x80000000 | 14, {{
            { QMetaType::QString, 43 }, { QMetaType::QStringList, 44 }, { QMetaType::Int, 45 }, { QMetaType::Int, 31 },
            { QMetaType::Int, 46 },
        }}),
        // Method 'messagesForSelection'
        QtMocHelpers::MethodData<QVariantList(const QString &, const QStringList &, int, int) const>(42, 2, QMC::AccessPublic | QMC::MethodCloned, 0x80000000 | 14, {{
            { QMetaType::QString, 43 }, { QMetaType::QStringList, 44 }, { QMetaType::Int, 45 }, { QMetaType::Int, 31 },
        }}),
        // Method 'messagesForSelection'
        QtMocHelpers::MethodData<QVariantList(const QString &, const QStringList &, int) const>(42, 2, QMC::AccessPublic | QMC::MethodCloned, 0x80000000 | 14, {{
            { QMetaType::QString, 43 }, { QMetaType::QStringList, 44 }, { QMetaType::Int, 45 },
        }}),
        // Method 'groupedMessagesForSelection'
        QtMocHelpers::MethodData<QVariantList(const QString &, const QStringList &, int, bool, bool, bool, bool, bool) const>(49, 2, QMC::AccessPublic, 0x80000000 | 14, {{
            { QMetaType::QString, 43 }, { QMetaType::QStringList, 44 }, { QMetaType::Int, 45 }, { QMetaType::Bool, 50 },
            { QMetaType::Bool, 51 }, { QMetaType::Bool, 52 }, { QMetaType::Bool, 53 }, { QMetaType::Bool, 54 },
        }}),
        // Method 'statsForFolder'
        QtMocHelpers::MethodData<QVariantMap(const QString &, const QString &) const>(55, 2, QMC::AccessPublic, 0x80000000 | 11, {{
            { QMetaType::QString, 43 }, { QMetaType::QString, 56 },
        }}),
        // Method 'hasCachedHeadersForFolder'
        QtMocHelpers::MethodData<bool(const QString &, int) const>(57, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 56 }, { QMetaType::Int, 58 },
        }}),
        // Method 'hasCachedHeadersForFolder'
        QtMocHelpers::MethodData<bool(const QString &) const>(57, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 56 },
        }}),
        // Method 'inboxCategoryTabs'
        QtMocHelpers::MethodData<QStringList() const>(59, 2, QMC::AccessPublic, QMetaType::QStringList),
        // Method 'tagItems'
        QtMocHelpers::MethodData<QVariantList() const>(60, 2, QMC::AccessPublic, 0x80000000 | 14),
        // Method 'avatarForEmail'
        QtMocHelpers::MethodData<QString(const QString &) const>(61, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 62 },
        }}),
        // Method 'displayNameForEmail'
        QtMocHelpers::MethodData<QString(const QString &) const>(63, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 62 },
        }}),
        // Method 'preferredSelfDisplayName'
        QtMocHelpers::MethodData<QString(const QString &) const>(64, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 5 },
        }}),
        // Method 'migrationStats'
        QtMocHelpers::MethodData<QVariantMap() const>(65, 2, QMC::AccessPublic, 0x80000000 | 11),
        // Method 'avatarShouldRefresh'
        QtMocHelpers::MethodData<bool(const QString &, int, int) const>(66, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 62 }, { QMetaType::Int, 67 }, { QMetaType::Int, 68 },
        }}),
        // Method 'avatarShouldRefresh'
        QtMocHelpers::MethodData<bool(const QString &, int) const>(66, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 62 }, { QMetaType::Int, 67 },
        }}),
        // Method 'avatarShouldRefresh'
        QtMocHelpers::MethodData<bool(const QString &) const>(66, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 62 },
        }}),
        // Method 'staleGooglePeopleEmails'
        QtMocHelpers::MethodData<QStringList(int) const>(69, 2, QMC::AccessPublic, QMetaType::QStringList, {{
            { QMetaType::Int, 31 },
        }}),
        // Method 'staleGooglePeopleEmails'
        QtMocHelpers::MethodData<QStringList() const>(69, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QStringList),
        // Method 'updateContactAvatar'
        QtMocHelpers::MethodData<void(const QString &, const QString &, const QString &)>(70, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 62 }, { QMetaType::QString, 71 }, { QMetaType::QString, 72 },
        }}),
        // Method 'isSenderTrusted'
        QtMocHelpers::MethodData<bool(const QString &) const>(73, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 74 },
        }}),
        // Method 'setTrustedSenderDomain'
        QtMocHelpers::MethodData<void(const QString &)>(75, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 74 },
        }}),
        // Method 'attachmentsForMessage'
        QtMocHelpers::MethodData<QVariantList(const QString &, const QString &, const QString &) const>(76, 2, QMC::AccessPublic, 0x80000000 | 14, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 8 }, { QMetaType::QString, 6 },
        }}),
        // Method 'searchContacts'
        QtMocHelpers::MethodData<QVariantList(const QString &, int) const>(77, 2, QMC::AccessPublic, 0x80000000 | 14, {{
            { QMetaType::QString, 78 }, { QMetaType::Int, 31 },
        }}),
        // Method 'searchContacts'
        QtMocHelpers::MethodData<QVariantList(const QString &) const>(77, 2, QMC::AccessPublic | QMC::MethodCloned, 0x80000000 | 14, {{
            { QMetaType::QString, 78 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'inbox'
        QtMocHelpers::PropertyData<QVariantList>(79, 0x80000000 | 14, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 0),
        // property 'folders'
        QtMocHelpers::PropertyData<QVariantList>(80, 0x80000000 | 14, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 1),
        // property 'inboxCategoryTabs'
        QtMocHelpers::PropertyData<QStringList>(59, QMetaType::QStringList, QMC::DefaultPropertyFlags, 0),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DataStore, qt_meta_tag_ZN9DataStoreE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject DataStore::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9DataStoreE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9DataStoreE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN9DataStoreE_t>.metaTypes,
    nullptr
} };

void DataStore::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DataStore *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->inboxChanged(); break;
        case 1: _t->foldersChanged(); break;
        case 2: _t->messageMarkedRead((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 3: _t->bodyHtmlUpdated((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 4: { bool _r = _t->init();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 5: _t->upsertHeader((*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[1]))); break;
        case 6: _t->upsertHeaders((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1]))); break;
        case 7: _t->upsertFolder((*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[1]))); break;
        case 8: _t->pruneFolderToUids((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[3]))); break;
        case 9: _t->removeAccountUidsEverywhere((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3]))); break;
        case 10: _t->removeAccountUidsEverywhere((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2]))); break;
        case 11: _t->markMessageRead((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 12: { QStringList _r = _t->folderUids((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 13: { qint64 _r = _t->folderMaxUid((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<qint64*>(_a[0]) = std::move(_r); }  break;
        case 14: { qint64 _r = _t->folderMessageCount((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<qint64*>(_a[0]) = std::move(_r); }  break;
        case 15: { QVariantMap _r = _t->folderSyncStatus((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 16: _t->upsertFolderSyncStatus((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[5]))); break;
        case 17: { QStringList _r = _t->bodyFetchCandidates((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 18: { QStringList _r = _t->bodyFetchCandidates((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 19: { QVariantList _r = _t->bodyFetchCandidatesByAccount((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 20: { QVariantList _r = _t->bodyFetchCandidatesByAccount((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 21: { QVariantList _r = _t->fetchCandidatesForMessageKey((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 22: { QVariantMap _r = _t->messageByKey((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 23: { QVariantList _r = _t->messagesForThread((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 24: { bool _r = _t->hasUsableBodyForEdge((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 25: { bool _r = _t->updateBodyForKey((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 26: _t->reloadInbox(); break;
        case 27: _t->reloadFolders(); break;
        case 28: { QVariantList _r = _t->messagesForSelection((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<bool*>>(_a[6])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 29: { QVariantList _r = _t->messagesForSelection((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[5])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 30: { QVariantList _r = _t->messagesForSelection((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 31: { QVariantList _r = _t->messagesForSelection((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 32: { QVariantList _r = _t->groupedMessagesForSelection((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[6])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[7])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[8])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 33: { QVariantMap _r = _t->statsForFolder((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 34: { bool _r = _t->hasCachedHeadersForFolder((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 35: { bool _r = _t->hasCachedHeadersForFolder((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 36: { QStringList _r = _t->inboxCategoryTabs();
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 37: { QVariantList _r = _t->tagItems();
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 38: { QString _r = _t->avatarForEmail((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 39: { QString _r = _t->displayNameForEmail((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 40: { QString _r = _t->preferredSelfDisplayName((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 41: { QVariantMap _r = _t->migrationStats();
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 42: { bool _r = _t->avatarShouldRefresh((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 43: { bool _r = _t->avatarShouldRefresh((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 44: { bool _r = _t->avatarShouldRefresh((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 45: { QStringList _r = _t->staleGooglePeopleEmails((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 46: { QStringList _r = _t->staleGooglePeopleEmails();
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        case 47: _t->updateContactAvatar((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 48: { bool _r = _t->isSenderTrusted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 49: _t->setTrustedSenderDomain((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 50: { QVariantList _r = _t->attachmentsForMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 51: { QVariantList _r = _t->searchContacts((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 52: { QVariantList _r = _t->searchContacts((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (DataStore::*)()>(_a, &DataStore::inboxChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (DataStore::*)()>(_a, &DataStore::foldersChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (DataStore::*)(const QString & , const QString & )>(_a, &DataStore::messageMarkedRead, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (DataStore::*)(const QString & , const QString & , const QString & )>(_a, &DataStore::bodyHtmlUpdated, 3))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QVariantList*>(_v) = _t->inbox(); break;
        case 1: *reinterpret_cast<QVariantList*>(_v) = _t->folders(); break;
        case 2: *reinterpret_cast<QStringList*>(_v) = _t->inboxCategoryTabs(); break;
        default: break;
        }
    }
}

const QMetaObject *DataStore::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DataStore::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9DataStoreE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int DataStore::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 53)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 53;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 53)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 53;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void DataStore::inboxChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void DataStore::foldersChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void DataStore::messageMarkedRead(const QString & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}

// SIGNAL 3
void DataStore::bodyHtmlUpdated(const QString & _t1, const QString & _t2, const QString & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2, _t3);
}
QT_WARNING_POP
