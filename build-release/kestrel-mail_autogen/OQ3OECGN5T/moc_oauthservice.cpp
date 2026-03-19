/****************************************************************************
** Meta object code from reading C++ file 'oauthservice.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/auth/oauthservice.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'oauthservice.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN12OAuthServiceE_t {};
} // unnamed namespace

template <> constexpr inline auto OAuthService::qt_create_metaobjectdata<qt_meta_tag_ZN12OAuthServiceE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "OAuthService",
        "pendingAuthUrlChanged",
        "",
        "lastStatusChanged",
        "authorizationCompleted",
        "ok",
        "message",
        "startAuthorization",
        "QVariantMap",
        "provider",
        "email",
        "completeAuthorization",
        "callbackOrCode",
        "hasStoredRefreshToken",
        "removeStoredRefreshToken",
        "profileForEmail",
        "pendingAuthUrl",
        "lastStatus"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'pendingAuthUrlChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'lastStatusChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'authorizationCompleted'
        QtMocHelpers::SignalData<void(bool, const QString &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 5 }, { QMetaType::QString, 6 },
        }}),
        // Method 'startAuthorization'
        QtMocHelpers::MethodData<QString(const QVariantMap &, const QString &)>(7, 2, QMC::AccessPublic, QMetaType::QString, {{
            { 0x80000000 | 8, 9 }, { QMetaType::QString, 10 },
        }}),
        // Method 'completeAuthorization'
        QtMocHelpers::MethodData<bool(const QString &)>(11, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 12 },
        }}),
        // Method 'hasStoredRefreshToken'
        QtMocHelpers::MethodData<bool(const QString &) const>(13, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 10 },
        }}),
        // Method 'removeStoredRefreshToken'
        QtMocHelpers::MethodData<bool(const QString &)>(14, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 10 },
        }}),
        // Method 'profileForEmail'
        QtMocHelpers::MethodData<QVariantMap(const QString &) const>(15, 2, QMC::AccessPublic, 0x80000000 | 8, {{
            { QMetaType::QString, 10 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'pendingAuthUrl'
        QtMocHelpers::PropertyData<QString>(16, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'lastStatus'
        QtMocHelpers::PropertyData<QString>(17, QMetaType::QString, QMC::DefaultPropertyFlags, 1),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<OAuthService, qt_meta_tag_ZN12OAuthServiceE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject OAuthService::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12OAuthServiceE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12OAuthServiceE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12OAuthServiceE_t>.metaTypes,
    nullptr
} };

void OAuthService::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<OAuthService *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->pendingAuthUrlChanged(); break;
        case 1: _t->lastStatusChanged(); break;
        case 2: _t->authorizationCompleted((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 3: { QString _r = _t->startAuthorization((*reinterpret_cast<std::add_pointer_t<QVariantMap>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 4: { bool _r = _t->completeAuthorization((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 5: { bool _r = _t->hasStoredRefreshToken((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 6: { bool _r = _t->removeStoredRefreshToken((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 7: { QVariantMap _r = _t->profileForEmail((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (OAuthService::*)()>(_a, &OAuthService::pendingAuthUrlChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (OAuthService::*)()>(_a, &OAuthService::lastStatusChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (OAuthService::*)(bool , const QString & )>(_a, &OAuthService::authorizationCompleted, 2))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->pendingAuthUrl(); break;
        case 1: *reinterpret_cast<QString*>(_v) = _t->lastStatus(); break;
        default: break;
        }
    }
}

const QMetaObject *OAuthService::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *OAuthService::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12OAuthServiceE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int OAuthService::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 8;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void OAuthService::pendingAuthUrlChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void OAuthService::lastStatusChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void OAuthService::authorizationCompleted(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}
QT_WARNING_POP
