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


TodayStats todayStats(const DynArray<MonthTask>& tasks, const std::string& now)
{
    TodayStats st;
    long nowMin = 0;
    if (!toMinutes(now, nowMin)) return st;
    long today      = nowMin / 1440;              // day number since 1970
    long endOfToday = (today + 1) * 1440;         // first minute of tomorrow

    DynArray<long> doneDays;
    for (const MonthTask& t : tasks) {
        long at = 0;
        if (t.completed) {
            if (!t.completedAt.empty() && toMinutes(t.completedAt, at)) {
                doneDays.push_back(at / 1440);
                if (at / 1440 == today) ++st.doneToday;
            }
        } else if (!t.deadline.empty() && toMinutes(t.deadline, at) && at < endOfToday) {
            ++st.openToday;                        // due by tonight, or overdue
        }
    }

    // Streak: walk back day by day from today through the distinct days
    // that have a completion, newest first, until a day is missing.
    mergeSort(doneDays, [](long a, long b) { return a > b; });
    DynArray<long> days;
    for (long d : doneDays)
        if (days.empty() || days.back() != d) days.push_back(d);

    std::size_t i = 0;
    while (i < days.size() && days[i] > today) ++i;                   // ignore future dates
    long expect = (i < days.size() && days[i] == today) ? today : today - 1;
    while (i < days.size() && days[i] == expect) { ++st.streak; --expect; ++i; }
    return st;
}


namespace {

// The calendar date of a day number (the reverse of daysFromCivil).
void civilFromDays(long z, int& y, int& m, int& d)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;                                   // day of the era
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);             // day of the year, March first
    long mp  = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

const char* kWeekdays[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

std::string hoursText(int minutes)
{
    int h = minutes / 60, m = minutes % 60;
    if (h == 0) return std::to_string(m) + "m";
    return m ? std::to_string(h) + "h " + std::to_string(m) + "m" : std::to_string(h) + "h";
}

} // namespace


DynArray<DayCount> lastDaysCompleted(const DynArray<MonthTask>& tasks, const std::string& now, int days)
{
    DynArray<DayCount> out;
    long nowMin = 0;
    if (days <= 0 || !toMinutes(now, nowMin)) return out;
    long today = nowMin / 1440;

    for (int i = days - 1; i >= 0; --i) {
        DayCount dc;
        dc.day     = today - i;
        dc.weekday = static_cast<int>((dc.day % 7 + 7 + 3) % 7);   // 1970-01-01 was a Thursday
        int y = 0, m = 0;
        civilFromDays(dc.day, y, m, dc.dayOfMonth);
        out.push_back(dc);
    }
    long first = today - (days - 1);
    for (const MonthTask& t : tasks) {
        long at = 0;
        if (t.completed && !t.completedAt.empty() && toMinutes(t.completedAt, at)) {
            long day = at / 1440;
            if (day >= first && day <= today) ++out[static_cast<std::size_t>(day - first)].count;
        }
    }
    return out;
}


DynArray<Insight> buildInsights(const DynArray<MonthTask>& tasks, const std::string& now, int freeMinutes)
{
    DynArray<Insight> out;
    long nowMin = 0;
    if (!toMinutes(now, nowMin)) return out;
    long endOfToday = (nowMin / 1440 + 1) * 1440;

    int  overdue = 0, pending = 0, withDeadline = 0, dueTodayMin = 0;
    long oldest = 0;
    std::string oldestTitle;
    int  byPart[4] = {0, 0, 0, 0};        // morning, afternoon, evening, night
    int  byWeekday[7] = {0, 0, 0, 0, 0, 0, 0};
    int  doneWithTime = 0;

    for (const MonthTask& t : tasks) {
        long at = 0;
        if (t.completed) {
            if (!t.completedAt.empty() && toMinutes(t.completedAt, at)) {
                ++doneWithTime;
                int hour = static_cast<int>((at % 1440) / 60);
                int part = (hour >= 5 && hour < 12) ? 0 : (hour >= 12 && hour < 17) ? 1 : (hour >= 17 && hour < 22) ? 2 : 3;
                ++byPart[part];
                ++byWeekday[(at / 1440 % 7 + 7 + 3) % 7];
            }
            continue;
        }
        ++pending;
        if (!t.deadline.empty() && toMinutes(t.deadline, at)) {
            ++withDeadline;
            if (at < nowMin) {
                if (overdue == 0 || at < oldest) { oldest = at; oldestTitle = t.title; }
                ++overdue;
            }
            if (at < endOfToday) dueTodayMin += t.minTime;   // due tonight, or already overdue
        }
    }

    // 1. Overdue work comes first.
    if (overdue > 0)
        out.push_back({2, std::to_string(overdue) + (overdue == 1 ? " task is" : " tasks are") +
                          " overdue. Start with the oldest: " + oldestTitle + "."});

    // 2. Does today's deadline work fit in the time left?
    if (dueTodayMin > 0) {
        if (dueTodayMin > freeMinutes)
            out.push_back({2, "Today's deadlines need at least " + hoursText(dueTodayMin) + ", but only " +
                              hoursText(freeMinutes < 0 ? 0 : freeMinutes) + " is free. Something has to move."});
        else
            out.push_back({1, "Today's deadlines fit: they need at least " + hoursText(dueTodayMin) +
                              " of your " + hoursText(freeMinutes) + " free."});
    }

    // 3. The streak.
    TodayStats st = todayStats(tasks, now);
    if (st.doneToday >= 3)
        out.push_back({1, std::to_string(st.doneToday) + " tasks done today. A strong day."});
    else if (st.streak > 0 && st.doneToday == 0)
        out.push_back({0, "Finish one task today to keep your " + std::to_string(st.streak) + "-day streak."});

    // 4. Deadlines make suggestions better.
    if (pending >= 3 && withDeadline == 0)
        out.push_back({0, "None of your pending tasks has a deadline. Add some, and suggestions get sharper."});

    // 5. When do you finish things? Needs enough history to mean anything.
    if (doneWithTime >= 5) {
        static const char* parts[4] = {"morning", "afternoon", "evening", "night"};
        int best = 0;
        for (int i = 1; i < 4; ++i) if (byPart[i] > byPart[best]) best = i;
        if (byPart[best] * 100 >= doneWithTime * 40)
            out.push_back({0, std::string("You finish most tasks in the ") + parts[best] + " (" +
                              std::to_string(byPart[best]) + " of " + std::to_string(doneWithTime) +
                              "). Plan demanding work then."});
    }

    // 6. Your strongest day of the week.
    if (doneWithTime >= 7) {
        int best = 0;
        for (int i = 1; i < 7; ++i) if (byWeekday[i] > byWeekday[best]) best = i;
        out.push_back({0, std::string(kWeekdays[best]) + " is when you complete the most tasks (" +
                          std::to_string(byWeekday[best]) + ")."});
    }

    if (out.empty())
        out.push_back({1, "All clear: nothing overdue and nothing due today."});
    while (out.size() > 3) out.pop_back();
    return out;
}
