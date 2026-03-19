/****************************************************************************
** Meta object code from reading C++ file 'imaplabcontroller.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/tools/imaplabcontroller.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'imaplabcontroller.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN17ImapLabControllerE_t {};
} // unnamed namespace

template <> constexpr inline auto ImapLabController::qt_create_metaobjectdata<qt_meta_tag_ZN17ImapLabControllerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ImapLabController",
        "accountsChanged",
        "",
        "selectedAccountEmailChanged",
        "commandTextChanged",
        "outputChanged",
        "appendOutputChanged",
        "elapsedMsChanged",
        "runningChanged",
        "refreshAccounts",
        "applyTemplate",
        "index",
        "runCurrentCommand",
        "accounts",
        "QVariantList",
        "commandTemplates",
        "selectedAccountEmail",
        "commandText",
        "output",
        "appendOutput",
        "elapsedMs",
        "running"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'accountsChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'selectedAccountEmailChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'commandTextChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'outputChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'appendOutputChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'elapsedMsChanged'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'runningChanged'
        QtMocHelpers::SignalData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'refreshAccounts'
        QtMocHelpers::MethodData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'applyTemplate'
        QtMocHelpers::MethodData<void(int)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 11 },
        }}),
        // Method 'runCurrentCommand'
        QtMocHelpers::MethodData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'accounts'
        QtMocHelpers::PropertyData<QVariantList>(13, 0x80000000 | 14, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 0),
        // property 'commandTemplates'
        QtMocHelpers::PropertyData<QVariantList>(15, 0x80000000 | 14, QMC::DefaultPropertyFlags | QMC::EnumOrFlag | QMC::Constant),
        // property 'selectedAccountEmail'
        QtMocHelpers::PropertyData<QString>(16, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'commandText'
        QtMocHelpers::PropertyData<QString>(17, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 2),
        // property 'output'
        QtMocHelpers::PropertyData<QString>(18, QMetaType::QString, QMC::DefaultPropertyFlags, 3),
        // property 'appendOutput'
        QtMocHelpers::PropertyData<bool>(19, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 4),
        // property 'elapsedMs'
        QtMocHelpers::PropertyData<qint64>(20, QMetaType::LongLong, QMC::DefaultPropertyFlags, 5),
        // property 'running'
        QtMocHelpers::PropertyData<bool>(21, QMetaType::Bool, QMC::DefaultPropertyFlags, 6),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ImapLabController, qt_meta_tag_ZN17ImapLabControllerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ImapLabController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17ImapLabControllerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17ImapLabControllerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17ImapLabControllerE_t>.metaTypes,
    nullptr
} };

void ImapLabController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ImapLabController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->accountsChanged(); break;
        case 1: _t->selectedAccountEmailChanged(); break;
        case 2: _t->commandTextChanged(); break;
        case 3: _t->outputChanged(); break;
        case 4: _t->appendOutputChanged(); break;
        case 5: _t->elapsedMsChanged(); break;
        case 6: _t->runningChanged(); break;
        case 7: _t->refreshAccounts(); break;
        case 8: _t->applyTemplate((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 9: _t->runCurrentCommand(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (ImapLabController::*)()>(_a, &ImapLabController::accountsChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapLabController::*)()>(_a, &ImapLabController::selectedAccountEmailChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapLabController::*)()>(_a, &ImapLabController::commandTextChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapLabController::*)()>(_a, &ImapLabController::outputChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapLabController::*)()>(_a, &ImapLabController::appendOutputChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapLabController::*)()>(_a, &ImapLabController::elapsedMsChanged, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (ImapLabController::*)()>(_a, &ImapLabController::runningChanged, 6))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QVariantList*>(_v) = _t->accounts(); break;
        case 1: *reinterpret_cast<QVariantList*>(_v) = _t->commandTemplates(); break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->selectedAccountEmail(); break;
        case 3: *reinterpret_cast<QString*>(_v) = _t->commandText(); break;
        case 4: *reinterpret_cast<QString*>(_v) = _t->output(); break;
        case 5: *reinterpret_cast<bool*>(_v) = _t->appendOutput(); break;
        case 6: *reinterpret_cast<qint64*>(_v) = _t->elapsedMs(); break;
        case 7: *reinterpret_cast<bool*>(_v) = _t->running(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 2: _t->setSelectedAccountEmail(*reinterpret_cast<QString*>(_v)); break;
        case 3: _t->setCommandText(*reinterpret_cast<QString*>(_v)); break;
        case 5: _t->setAppendOutput(*reinterpret_cast<bool*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *ImapLabController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ImapLabController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17ImapLabControllerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int ImapLabController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 10;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void ImapLabController::accountsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void ImapLabController::selectedAccountEmailChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void ImapLabController::commandTextChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void ImapLabController::outputChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void ImapLabController::appendOutputChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void ImapLabController::elapsedMsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void ImapLabController::runningChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}
QT_WARNING_POP
