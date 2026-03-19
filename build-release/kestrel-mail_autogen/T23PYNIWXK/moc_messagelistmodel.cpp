/****************************************************************************
** Meta object code from reading C++ file 'messagelistmodel.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/store/messagelistmodel.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'messagelistmodel.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN16MessageListModelE_t {};
} // unnamed namespace

template <> constexpr inline auto MessageListModel::qt_create_metaobjectdata<qt_meta_tag_ZN16MessageListModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MessageListModel",
        "dataStoreChanged",
        "",
        "windowSizeChanged",
        "totalRowCountChanged",
        "visibleCountsChanged",
        "pagingChanged",
        "setSelection",
        "folderKey",
        "selectedCategories",
        "selectedCategoryIndex",
        "setBucketExpanded",
        "bucketKey",
        "expanded",
        "setExpansionState",
        "todayExpanded",
        "yesterdayExpanded",
        "lastWeekExpanded",
        "twoWeeksAgoExpanded",
        "olderExpanded",
        "refresh",
        "loadMore",
        "shiftWindowDown",
        "shiftWindowUp",
        "rowAt",
        "QVariantMap",
        "index",
        "setWindowSize",
        "size",
        "dataStore",
        "DataStore*",
        "windowSize",
        "totalRowCount",
        "visibleRowCount",
        "visibleMessageCount",
        "hasMore",
        "pageSize",
        "RowType",
        "HeaderRow",
        "MessageRow"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'dataStoreChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'windowSizeChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'totalRowCountChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'visibleCountsChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'pagingChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setSelection'
        QtMocHelpers::MethodData<void(const QString &, const QStringList &, int)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 }, { QMetaType::QStringList, 9 }, { QMetaType::Int, 10 },
        }}),
        // Method 'setBucketExpanded'
        QtMocHelpers::MethodData<void(const QString &, bool)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 12 }, { QMetaType::Bool, 13 },
        }}),
        // Method 'setExpansionState'
        QtMocHelpers::MethodData<void(bool, bool, bool, bool, bool)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 15 }, { QMetaType::Bool, 16 }, { QMetaType::Bool, 17 }, { QMetaType::Bool, 18 },
            { QMetaType::Bool, 19 },
        }}),
        // Method 'refresh'
        QtMocHelpers::MethodData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'loadMore'
        QtMocHelpers::MethodData<void()>(21, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'shiftWindowDown'
        QtMocHelpers::MethodData<void()>(22, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'shiftWindowUp'
        QtMocHelpers::MethodData<void()>(23, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'rowAt'
        QtMocHelpers::MethodData<QVariantMap(int) const>(24, 2, QMC::AccessPublic, 0x80000000 | 25, {{
            { QMetaType::Int, 26 },
        }}),
        // Method 'setWindowSize'
        QtMocHelpers::MethodData<void(int)>(27, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 28 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'dataStore'
        QtMocHelpers::PropertyData<DataStore*>(29, 0x80000000 | 30, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 0),
        // property 'windowSize'
        QtMocHelpers::PropertyData<int>(31, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'totalRowCount'
        QtMocHelpers::PropertyData<int>(32, QMetaType::Int, QMC::DefaultPropertyFlags, 2),
        // property 'visibleRowCount'
        QtMocHelpers::PropertyData<int>(33, QMetaType::Int, QMC::DefaultPropertyFlags, 3),
        // property 'visibleMessageCount'
        QtMocHelpers::PropertyData<int>(34, QMetaType::Int, QMC::DefaultPropertyFlags, 3),
        // property 'hasMore'
        QtMocHelpers::PropertyData<bool>(35, QMetaType::Bool, QMC::DefaultPropertyFlags, 4),
        // property 'pageSize'
        QtMocHelpers::PropertyData<int>(36, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Constant),
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'RowType'
        QtMocHelpers::EnumData<enum RowType>(37, 37, QMC::EnumFlags{}).add({
            {   38, RowType::HeaderRow },
            {   39, RowType::MessageRow },
        }),
    };
    return QtMocHelpers::metaObjectData<MessageListModel, qt_meta_tag_ZN16MessageListModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MessageListModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractListModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16MessageListModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16MessageListModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN16MessageListModelE_t>.metaTypes,
    nullptr
} };

void MessageListModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MessageListModel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->dataStoreChanged(); break;
        case 1: _t->windowSizeChanged(); break;
        case 2: _t->totalRowCountChanged(); break;
        case 3: _t->visibleCountsChanged(); break;
        case 4: _t->pagingChanged(); break;
        case 5: _t->setSelection((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3]))); break;
        case 6: _t->setBucketExpanded((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 7: _t->setExpansionState((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[5]))); break;
        case 8: _t->refresh(); break;
        case 9: _t->loadMore(); break;
        case 10: _t->shiftWindowDown(); break;
        case 11: _t->shiftWindowUp(); break;
        case 12: { QVariantMap _r = _t->rowAt((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 13: _t->setWindowSize((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (MessageListModel::*)()>(_a, &MessageListModel::dataStoreChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (MessageListModel::*)()>(_a, &MessageListModel::windowSizeChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (MessageListModel::*)()>(_a, &MessageListModel::totalRowCountChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (MessageListModel::*)()>(_a, &MessageListModel::visibleCountsChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (MessageListModel::*)()>(_a, &MessageListModel::pagingChanged, 4))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<DataStore**>(_v) = _t->dataStore(); break;
        case 1: *reinterpret_cast<int*>(_v) = _t->windowSize(); break;
        case 2: *reinterpret_cast<int*>(_v) = _t->totalRowCount(); break;
        case 3: *reinterpret_cast<int*>(_v) = _t->visibleRowCount(); break;
        case 4: *reinterpret_cast<int*>(_v) = _t->visibleMessageCount(); break;
        case 5: *reinterpret_cast<bool*>(_v) = _t->hasMore(); break;
        case 6: *reinterpret_cast<int*>(_v) = _t->pageSize(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setDataStore(*reinterpret_cast<DataStore**>(_v)); break;
        case 1: _t->setWindowSize(*reinterpret_cast<int*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *MessageListModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MessageListModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16MessageListModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractListModel::qt_metacast(_clname);
}

int MessageListModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractListModel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 14;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void MessageListModel::dataStoreChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void MessageListModel::windowSizeChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void MessageListModel::totalRowCountChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void MessageListModel::visibleCountsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void MessageListModel::pagingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
