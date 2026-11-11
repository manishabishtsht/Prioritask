#include "monthly.h"

#include <cstdio>

namespace {

// Days from 1970-01-01 to the given date, for any date in the Gregorian
// calendar. Treats March as the first month of the year, so the leap day
// falls at the end and the month lengths follow a simple pattern.
long daysFromCivil(int y, int m, int d)
{
    y -= (m <= 2) ? 1 : 0;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;                                  // year of the era, 0..399
    long mp  = (m > 2) ? m - 3 : m + 9;                        // March = 0 ... February = 11
    long doy = (153 * mp + 2) / 5 + d - 1;                     // day of the year, 0..365
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // day of the era
    return era * 146097 + doe - 719468;
}

void nextMonth(int year, int month, int& ny, int& nm)
{
    ny = (month == 12) ? year + 1 : year;
    nm = (month == 12) ? 1 : month + 1;
}

} // namespace


int daysInMonth(int year, int month)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return days[month - 1] + ((month == 2 && leap) ? 1 : 0);
}

bool toMinutes(const std::string& stamp, long& out)
{
    int y = 0, mo = 0, d = 0, h = 23, mi = 59;
    int n = std::sscanf(stamp.c_str(), "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi);
    if (n == 3) { h = 23; mi = 59; }              // a bare date means the end of that day
    else if (n != 5) return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59) return false;

    out = daysFromCivil(y, mo, d) * 1440 + h * 60 + mi;
    return true;
}


MonthReport buildMonthReport(const DynArray<MonthTask>& tasks,
                             const DynArray<std::string>& planDates,
                             int year, int month,
                             const std::string& now)
{
    MonthReport r;
    r.year            = year;
    r.month           = month;
    r.daysInMonth     = daysInMonth(year, month);
    r.completedPerDay = DynArray<int>(static_cast<std::size_t>(r.daysInMonth), 0);

    // The month is the range [monthStart, nextStart) in minutes.
    int ny = 0, nm = 0;
    nextMonth(year, month, ny, nm);
    long monthStart = daysFromCivil(year, month, 1) * 1440;
    long nextStart  = daysFromCivil(ny, nm, 1) * 1440;

    long nowMin  = 0;
    bool haveNow = toMinutes(now, nowMin);

    for (const MonthTask& t : tasks) {
        long deadline = 0, doneAt = 0;
        bool hasDeadline = !t.deadline.empty() && toMinutes(t.deadline, deadline);
        bool hasDoneTime = t.completed && !t.completedAt.empty() && toMinutes(t.completedAt, doneAt);

        MonthItem item;
        item.id       = t.id;
        item.title    = t.title;
        item.priority = t.priority;
        item.deadline = t.deadline;

        // ---- Completed during this month: on time or late ----
        if (hasDoneTime && doneAt >= monthStart && doneAt < nextStart) {
            ++r.completed;
            ++r.completedPerDay[static_cast<std::size_t>((doneAt - monthStart) / 1440)];

            if (hasDeadline && doneAt > deadline) {
                ++r.late;
                item.minutesLate = doneAt - deadline;
                r.lateTasks.push_back(item);
            } else {
                ++r.onTime;            // before the deadline, or no deadline at all
            }
        }

        // ---- Left undone: the deadline fell in this month and has passed,
        //      and the task was not finished by the end of the month ----
        if (t.completed && !hasDoneTime) continue;    // done, but we can't tell when
        bool deadlineInMonth = hasDeadline && deadline >= monthStart && deadline < nextStart;
        if (deadlineInMonth && haveNow && deadline < nowMin) {
            bool doneByMonthEnd = hasDoneTime && doneAt < nextStart;
            if (!doneByMonthEnd) {
                ++r.undone;
                item.stillPending = !hasDoneTime;
                item.minutesLate  = (hasDoneTime ? doneAt : nowMin) - deadline;
                r.undoneTasks.push_back(item);
            }
        }
    }

    // On-time rate: the share of this month's work that was done on time.
    int rated = r.onTime + r.late + r.undone;
    if (rated > 0) r.onTimeRate = (r.onTime * 100 + rated / 2) / rated;

    // Days planned: each day of the month counted once, however many rows.
    DynArray<bool> planned(static_cast<std::size_t>(r.daysInMonth), false);
    for (const std::string& date : planDates) {
        int y = 0, m = 0, d = 0;
        if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) == 3 &&
            y == year && m == month && d >= 1 && d <= r.daysInMonth) {
            planned[static_cast<std::size_t>(d - 1)] = true;
        }
    }
    for (bool p : planned) if (p) ++r.daysPlanned;

    // Most urgent first: highest priority, then most overdue.
    mergeSort(r.undoneTasks, [](const MonthItem& a, const MonthItem& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.minutesLate != b.minutesLate) return a.minutesLate > b.minutesLate;
        return a.id < b.id;
    });
    mergeSort(r.lateTasks, [](const MonthItem& a, const MonthItem& b) {
        if (a.minutesLate != b.minutesLate) return a.minutesLate > b.minutesLate;
        return a.id < b.id;
    });

    return r;
}
