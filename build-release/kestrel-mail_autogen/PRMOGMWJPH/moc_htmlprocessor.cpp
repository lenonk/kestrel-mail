/****************************************************************************
** Meta object code from reading C++ file 'htmlprocessor.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/htmlprocessor.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'htmlprocessor.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN13HtmlProcessorE_t {};
} // unnamed namespace

template <> constexpr inline auto HtmlProcessor::qt_create_metaobjectdata<qt_meta_tag_ZN13HtmlProcessorE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "HtmlProcessor",
        "sanitize",
        "",
        "rawHtml",
        "prepare",
        "html",
        "darkMode",
        "prepareThread",
        "collapseBlockquotes",
        "neutralizeExternalImages",
        "neutralizeTrackingPixels",
        "senderDomain",
        "darkBg",
        "surfaceBg",
        "lightText",
        "borderColor"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'sanitize'
        QtMocHelpers::MethodData<QString(const QString &) const>(1, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 3 },
        }}),
        // Method 'prepare'
        QtMocHelpers::MethodData<QString(const QString &, bool) const>(4, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 5 }, { QMetaType::Bool, 6 },
        }}),
        // Method 'prepareThread'
        QtMocHelpers::MethodData<QString(const QString &, bool) const>(7, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 5 }, { QMetaType::Bool, 6 },
        }}),
        // Method 'collapseBlockquotes'
        QtMocHelpers::MethodData<QString(const QString &) const>(8, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 5 },
        }}),
        // Method 'neutralizeExternalImages'
        QtMocHelpers::MethodData<QString(const QString &) const>(9, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 5 },
        }}),
        // Method 'neutralizeTrackingPixels'
        QtMocHelpers::MethodData<QString(const QString &, const QString &) const>(10, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 11 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'darkBg'
        QtMocHelpers::PropertyData<QString>(12, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable),
        // property 'surfaceBg'
        QtMocHelpers::PropertyData<QString>(13, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable),
        // property 'lightText'
        QtMocHelpers::PropertyData<QString>(14, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable),
        // property 'borderColor'
        QtMocHelpers::PropertyData<QString>(15, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<HtmlProcessor, qt_meta_tag_ZN13HtmlProcessorE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject HtmlProcessor::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13HtmlProcessorE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13HtmlProcessorE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN13HtmlProcessorE_t>.metaTypes,
    nullptr
} };

void HtmlProcessor::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<HtmlProcessor *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: { QString _r = _t->sanitize((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 1: { QString _r = _t->prepare((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 2: { QString _r = _t->prepareThread((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 3: { QString _r = _t->collapseBlockquotes((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 4: { QString _r = _t->neutralizeExternalImages((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 5: { QString _r = _t->neutralizeTrackingPixels((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->m_darkBg; break;
        case 1: *reinterpret_cast<QString*>(_v) = _t->m_surfaceBg; break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->m_lightText; break;
        case 3: *reinterpret_cast<QString*>(_v) = _t->m_borderColor; break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: QtMocHelpers::setProperty(_t->m_darkBg, *reinterpret_cast<QString*>(_v)); break;
        case 1: QtMocHelpers::setProperty(_t->m_surfaceBg, *reinterpret_cast<QString*>(_v)); break;
        case 2: QtMocHelpers::setProperty(_t->m_lightText, *reinterpret_cast<QString*>(_v)); break;
        case 3: QtMocHelpers::setProperty(_t->m_borderColor, *reinterpret_cast<QString*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *HtmlProcessor::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *HtmlProcessor::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13HtmlProcessorE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int HtmlProcessor::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    return _id;
}
QT_WARNING_POP
