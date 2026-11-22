#ifndef MONTHLY_H
#define MONTHLY_H

#include <string>

#include "dsa.h"

// ---------------------------------------------------------------
// Monthly report: how many tasks were done this month, how many on
// time, and which were left undone.
//
// Like the scheduler and advisor, this knows nothing about SQLite or
// the clock. The interface passes plain records and the current time
// in, so every rule can be tested with fixed inputs.
//
// All times are local time strings:
//   deadline     "YYYY-MM-DD HH:MM", or "YYYY-MM-DD" meaning 23:59 that day
//   completedAt  "YYYY-MM-DD HH:MM:SS"
// ---------------------------------------------------------------

struct MonthTask {
    int         id = 0;
    std::string title;
    int         priority = 3;
    std::string deadline;      // empty if the task has no deadline
    bool        completed = false;
    std::string completedAt;   // empty if not completed
    int         minTime = 0;   // minimum minutes (used by the insights)
};

// One task shown in the "left undone" or "finished late" list.
struct MonthItem {
    int         id = 0;
    std::string title;
    int         priority = 3;
    std::string deadline;          // as stored
    long        minutesLate = 0;   // past the deadline: until completion, or until now
    bool        stillPending = false;
};

struct MonthReport {
    int year = 0, month = 0;       // month is 1..12
    int daysInMonth = 0;

    int completed = 0;             // marked Done during the month
    int onTime    = 0;             // ...before the deadline, or with no deadline
    int late      = 0;             // ...after the deadline
    int undone    = 0;             // deadline in the month passed without the task being done
    int onTimeRate = -1;           // percent; -1 when there is nothing to rate yet
    int daysPlanned = 0;           // days in the month with a saved plan

    DynArray<int>       completedPerDay;   // index 0 = day 1 of the month
    DynArray<MonthItem> undoneTasks;       // highest priority first, then most overdue
    DynArray<MonthItem> lateTasks;         // most late first
};

// planDates: dates that have a saved plan, "YYYY-MM-DD" (any month).
// now:       the current local time, "YYYY-MM-DD HH:MM".
MonthReport buildMonthReport(const DynArray<MonthTask>& tasks,
                             const DynArray<std::string>& planDates,
                             int year, int month,
                             const std::string& now);

int  daysInMonth(int year, int month);

// "2026-11-10 14:30" -> minutes since 1970-01-01 00:00. Returns false if
// the text is not a date. Used to measure how late a task was.
bool toMinutes(const std::string& stamp, long& out);

// Today's progress, for the ring and streak on the What now tab.
struct TodayStats {
    int doneToday = 0;   // tasks completed today
    int openToday = 0;   // tasks still pending that are due today or already overdue
    int streak    = 0;   // days in a row with at least one task done, ending today
                         // (or yesterday, if nothing is done yet today)
};

// now: the current local time, "YYYY-MM-DD HH:MM".
TodayStats todayStats(const DynArray<MonthTask>& tasks, const std::string& now);

// One day in the "last 7 days" chart.
struct DayCount {
    long day        = 0;   // days since 1970-01-01
    int  weekday    = 0;   // 0 = Monday ... 6 = Sunday
    int  dayOfMonth = 0;
    int  count      = 0;   // tasks completed that day
};

// The last `days` days up to and including today, oldest first.
DynArray<DayCount> lastDaysCompleted(const DynArray<MonthTask>& tasks, const std::string& now, int days);

// A short tip worked out from the user's own data, by fixed rules.
struct Insight {
    int         kind = 0;   // 0 = tip, 1 = good news, 2 = warning
    std::string text;
};

// Up to three insights, most urgent first.
// freeMinutes: time free from now until wake-up, after commitments.
DynArray<Insight> buildInsights(const DynArray<MonthTask>& tasks, const std::string& now, int freeMinutes);

#endif
