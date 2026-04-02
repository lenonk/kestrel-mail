#pragma once

#include <QObject>
#include <QVariantList>

class CalendarLayoutHelper : public QObject
{
    Q_OBJECT
public:
    explicit CalendarLayoutHelper(QObject *parent = nullptr);

    /// Compute sub-column layout for overlapping timed events.
    /// Each input event is a QVariantMap with at least:
    ///   dayIndex (int), startHour (real), durationHours (real)
    /// Returns a QVariantList of QVariantMaps, each containing:
    ///   data (original event map), subCol (int), totalCols (int)
    Q_INVOKABLE QVariantList computeLayout(const QVariantList &events, int dayCount) const;
};
