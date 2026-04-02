#pragma once
#include <QString>

namespace Kestrel {
inline QString normalizeEmail(const QString &email) { return email.trimmed().toLower(); }
}
