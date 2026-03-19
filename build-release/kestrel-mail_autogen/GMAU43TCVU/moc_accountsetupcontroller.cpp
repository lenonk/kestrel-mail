/****************************************************************************
** Meta object code from reading C++ file 'accountsetupcontroller.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/accounts/accountsetupcontroller.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'accountsetupcontroller.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN22AccountSetupControllerE_t {};
} // unnamed namespace

template <> constexpr inline auto AccountSetupController::qt_create_metaobjectdata<qt_meta_tag_ZN22AccountSetupControllerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AccountSetupController",
        "emailChanged",
        "",
        "selectedProviderChanged",
        "oauthUrlChanged",
        "statusMessageChanged",
        "oauthReadyChanged",
        "discoverProvider",
        "beginOAuth",
        "completeOAuth",
        "callbackOrCode",
        "saveCurrentAccount",
        "accountName",
        "encryption",
        "hasTokenForEmail",
        "email",
        "removeAccount",
        "selectedProvider",
        "QVariantMap",
        "oauthUrl",
        "statusMessage",
        "oauthReady"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'emailChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'selectedProviderChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'oauthUrlChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'statusMessageChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'oauthReadyChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'discoverProvider'
        QtMocHelpers::MethodData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'beginOAuth'
        QtMocHelpers::MethodData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'completeOAuth'
        QtMocHelpers::MethodData<bool(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 10 },
        }}),
        // Method 'saveCurrentAccount'
        QtMocHelpers::MethodData<bool(const QString &, const QString &)>(11, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 12 }, { QMetaType::QString, 13 },
        }}),
        // Method 'saveCurrentAccount'
        QtMocHelpers::MethodData<bool(const QString &)>(11, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 12 },
        }}),
        // Method 'saveCurrentAccount'
        QtMocHelpers::MethodData<bool()>(11, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool),
        // Method 'hasTokenForEmail'
        QtMocHelpers::MethodData<bool(const QString &) const>(14, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 15 },
        }}),
        // Method 'removeAccount'
        QtMocHelpers::MethodData<bool(const QString &)>(16, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 15 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'email'
        QtMocHelpers::PropertyData<QString>(15, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'selectedProvider'
        QtMocHelpers::PropertyData<QVariantMap>(17, 0x80000000 | 18, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 1),
        // property 'oauthUrl'
        QtMocHelpers::PropertyData<QString>(19, QMetaType::QString, QMC::DefaultPropertyFlags, 2),
        // property 'statusMessage'
        QtMocHelpers::PropertyData<QString>(20, QMetaType::QString, QMC::DefaultPropertyFlags, 3),
        // property 'oauthReady'
        QtMocHelpers::PropertyData<bool>(21, QMetaType::Bool, QMC::DefaultPropertyFlags, 4),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AccountSetupController, qt_meta_tag_ZN22AccountSetupControllerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AccountSetupController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN22AccountSetupControllerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN22AccountSetupControllerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN22AccountSetupControllerE_t>.metaTypes,
    nullptr
} };

void AccountSetupController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AccountSetupController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->emailChanged(); break;
        case 1: _t->selectedProviderChanged(); break;
        case 2: _t->oauthUrlChanged(); break;
        case 3: _t->statusMessageChanged(); break;
        case 4: _t->oauthReadyChanged(); break;
        case 5: _t->discoverProvider(); break;
        case 6: _t->beginOAuth(); break;
        case 7: { bool _r = _t->completeOAuth((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 8: { bool _r = _t->saveCurrentAccount((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 9: { bool _r = _t->saveCurrentAccount((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 10: { bool _r = _t->saveCurrentAccount();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 11: { bool _r = _t->hasTokenForEmail((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 12: { bool _r = _t->removeAccount((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AccountSetupController::*)()>(_a, &AccountSetupController::emailChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AccountSetupController::*)()>(_a, &AccountSetupController::selectedProviderChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (AccountSetupController::*)()>(_a, &AccountSetupController::oauthUrlChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (AccountSetupController::*)()>(_a, &AccountSetupController::statusMessageChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (AccountSetupController::*)()>(_a, &AccountSetupController::oauthReadyChanged, 4))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->email(); break;
        case 1: *reinterpret_cast<QVariantMap*>(_v) = _t->selectedProvider(); break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->oauthUrl(); break;
        case 3: *reinterpret_cast<QString*>(_v) = _t->statusMessage(); break;
        case 4: *reinterpret_cast<bool*>(_v) = _t->oauthReady(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setEmail(*reinterpret_cast<QString*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *AccountSetupController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AccountSetupController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN22AccountSetupControllerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AccountSetupController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 13)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 13;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void AccountSetupController::emailChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void AccountSetupController::selectedProviderChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void AccountSetupController::oauthUrlChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void AccountSetupController::statusMessageChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void AccountSetupController::oauthReadyChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
