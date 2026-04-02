#include "calendarlayouthelper.h"

#include <algorithm>
#include <vector>

using namespace Qt::Literals::StringLiterals;

CalendarLayoutHelper::CalendarLayoutHelper(QObject *parent)
    : QObject(parent)
{
}

QVariantList CalendarLayoutHelper::computeLayout(const QVariantList &events, int dayCount) const
{
    if (events.isEmpty())
        return {};

    struct DayEvent {
        int idx;       // index into the original events list
        double start;
        double end;
        int subCol = 0;
        QVariantMap data;
    };

    QVariantList result;

    for (int d = 0; d < dayCount; ++d) {
        // Collect events for this day.
        std::vector<DayEvent> dayEvts;
        for (int i = 0; i < events.size(); ++i) {
            const auto map = events.at(i).toMap();
            const int dayIdx = map.value("dayIndex"_L1, 0).toInt();
            if (dayIdx != d)
                continue;

            const double startHour = map.value("startHour"_L1, 0.0).toDouble();
            const double duration  = map.value("durationHours"_L1, 0.25).toDouble();
            dayEvts.push_back({i, startHour, startHour + duration, 0, map});
        }

        // Sort by start time, then longer events first.
        std::sort(dayEvts.begin(), dayEvts.end(), [](const DayEvent &a, const DayEvent &b) {
            if (a.start != b.start)
                return a.start < b.start;
            return (b.end - b.start) < (a.end - a.start);
        });

        // Greedy column assignment: for each event, find the first column
        // where it doesn't overlap with any already-placed event.
        std::vector<std::vector<DayEvent *>> columns;
        for (auto &ev : dayEvts) {
            bool placed = false;
            for (size_t c = 0; c < columns.size(); ++c) {
                bool fits = true;
                for (const auto *existing : columns[c]) {
                    if (ev.start < existing->end && ev.end > existing->start) {
                        fits = false;
                        break;
                    }
                }
                if (fits) {
                    columns[c].push_back(&ev);
                    ev.subCol = static_cast<int>(c);
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                ev.subCol = static_cast<int>(columns.size());
                columns.push_back({&ev});
            }
        }

        // For each event, find the max subCol among all events that overlap it,
        // to determine the local column count (totalCols).
        for (const auto &e : dayEvts) {
            int maxCol = e.subCol;
            for (const auto &o : dayEvts) {
                if (e.start < o.end && e.end > o.start) {
                    if (o.subCol > maxCol)
                        maxCol = o.subCol;
                }
            }

            QVariantMap entry;
            entry["data"_L1]      = e.data;
            entry["subCol"_L1]    = e.subCol;
            entry["totalCols"_L1] = maxCol + 1;
            result.append(entry);
        }
    }

    return result;
}
