// PrioriTask - command line interface
//
// Build:
//   gcc -c sqlite3.c -o sqlite3.o                        (only once)
//   g++ -std=c++17 -Wall main.cpp scheduler.cpp sqlite3.o -o prioritask.exe
// Run:
//   .\prioritask.exe

#include "advisor.h"
#include "database.h"
#include "scheduler.h"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------
// Input helpers
//
// Everything is read with getline. Mixing  cin >> x  and getline is the
// classic source of "the program skipped my input" bugs, because >> leaves
// the newline sitting in the buffer. Reading whole lines avoids it.
// ---------------------------------------------------------------
namespace {

std::string readLine(const std::string& prompt)
{
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    return line;
}

// Keeps asking until the answer is a number inside [lo, hi].
// Typing letters would put cin into a fail state and loop forever if we
// used >> here, so the text is parsed by hand instead.
int readInt(const std::string& prompt, int lo, int hi)
{
    while (true) {
        std::string line = readLine(prompt);

        try {
            std::size_t pos = 0;
            int value = std::stoi(line, &pos);

            // Reject trailing junk like "45abc"
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
            if (pos != line.size()) throw std::invalid_argument("trailing");

            if (value < lo || value > hi) {
                std::cout << "  Please enter a number from " << lo << " to " << hi << ".\n";
                continue;
            }
            return value;
        }
        catch (const std::exception&) {
            std::cout << "  That is not a number. Try again.\n";
        }
    }
}

bool readYesNo(const std::string& prompt)
{
    while (true) {
        std::string s = readLine(prompt);
        if (s == "y" || s == "Y" || s == "yes") return true;
        if (s == "n" || s == "N" || s == "no")  return false;
        std::cout << "  Please answer y or n.\n";
    }
}

// ---------------------------------------------------------------
// Clock helpers
// ---------------------------------------------------------------

// Current local time, read from the device clock.
std::tm localNow()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::string todayString()
{
    std::tm tm = localNow();
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}

// Minutes since midnight, e.g. 21:30 -> 1290.
int nowMinutes()
{
    std::tm tm = localNow();
    return tm.tm_hour * 60 + tm.tm_min;
}

// 1290 -> "21:30"
std::string formatHM(int minutes)
{
    char buf[16];   // generous so the compiler can prove it never truncates
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return std::string(buf);
}

// "07:00" -> 420. Returns -1 if the text is not a valid HH:MM time.
int parseHM(const std::string& s)
{
    int h = 0, m = 0, used = 0;
    char colon = 0;

    // %n records how many characters were consumed; it is not counted in
    // the return value, which is why we compare against 3.
    if (std::sscanf(s.c_str(), "%d%c%d%n", &h, &colon, &m, &used) != 3) return -1;
    if (colon != ':')                              return -1;
    if (used != static_cast<int>(s.size()))        return -1;   // trailing junk
    if (h < 0 || h > 23 || m < 0 || m > 59) return -1;

    return h * 60 + m;
}

std::string readTime(const std::string& prompt)
{
    while (true) {
        std::string s = readLine(prompt);
        if (parseHM(s) >= 0) return formatHM(parseHM(s));   // normalises "7:0" to "07:00"
        std::cout << "  Please use HH:MM, for example 07:00 or 6:30.\n";
    }
}

// Minutes from now until the NEXT time the clock reads wakeMinutes.
//   now 21:30, wake 07:00  ->  570   (wake-up is tomorrow morning)
//   now 05:00, wake 07:00  ->  120   (wake-up is later this morning)
//   now 07:00, wake 07:00  -> 1440   (a full day ahead, not zero)
int minutesUntil(int wakeMinutes)
{
    int diff = wakeMinutes - nowMinutes();
    if (diff <= 0) diff += 24 * 60;
    return diff;
}

// Loads the saved wake-up time. If none has been saved yet, asks once
// and stores the answer so the user is never asked again.
int getWakeTime(Database& db)
{
    std::string saved;
    if (db.getSetting("wake_time", saved)) {
        int m = parseHM(saved);
        if (m >= 0) return m;
        // A corrupted value falls through and gets re-asked.
    }

    std::cout << "\nI don't know when you wake up yet. I'll only ask this once.\n";
    std::string t = readTime("What time do you wake up? (HH:MM): ");
    db.setSetting("wake_time", t);
    std::cout << "Saved. You can change it from the menu any time.\n";
    return parseHM(t);
}

// ---------------------------------------------------------------
// Menu actions
// ---------------------------------------------------------------

void listTasks(Database& db, bool pendingOnly)
{
    std::vector<Task> tasks = pendingOnly ? db.getPendingTasks() : db.getAllTasks();

    if (tasks.empty()) {
        std::cout << "\nNo tasks found.\n";
        return;
    }

    std::cout << "\n" << std::left
              << std::setw(5)  << "id"
              << std::setw(22) << "title"
              << std::right
              << std::setw(5)  << "pri"
              << std::setw(8)  << "wants"
              << std::setw(8)  << "floor"
              << "  " << std::left
              << std::setw(12) << "status"
              << "deadline" << "\n";
    std::cout << std::string(78, '-') << "\n";

    int totalWanted = 0;
    for (const Task& t : tasks) {
        std::cout << std::left
                  << std::setw(5)  << t.id
                  << std::setw(22) << t.title.substr(0, 21)
                  << std::right
                  << std::setw(5)  << t.priority
                  << std::setw(8)  << t.estimatedTime
                  << std::setw(8)  << t.minTime
                  << "  " << std::left
                  << std::setw(12) << t.status
                  << (t.deadline.empty() ? "-" : t.deadline) << "\n";
        totalWanted += t.estimatedTime;
    }
    std::cout << std::string(78, '-') << "\n";
    std::cout << tasks.size() << " task(s), " << totalWanted
              << " minutes requested in total.\n";
}

void addTask(Database& db)
{
    Task t;
    std::cout << "\n--- New task ---\n";

    while (t.title.empty()) {
        t.title = readLine("Title: ");
        if (t.title.empty()) std::cout << "  Title cannot be empty.\n";
    }

    t.description    = readLine("Description (optional): ");
    t.category       = readLine("Category (e.g. Study, Health): ");
    t.priority       = readInt("Priority 1-5 (5 = most important): ", 1, 5);
    t.difficulty     = readInt("Difficulty 1-5: ", 1, 5);
    t.energyRequired = readInt("Energy required 1-5: ", 1, 5);
    t.estimatedTime  = readInt("Ideal time in minutes: ", 1, 1440);

    // The floor cannot exceed the ideal, and the table has a CHECK that
    // would reject it. Catch it here with a clear message instead of
    // letting SQLite produce a cryptic one.
    t.minTime = readInt("Minimum acceptable time in minutes: ", 1, t.estimatedTime);

    t.isMandatory = readYesNo("Is this mandatory? (y/n): ");
    t.deadline    = readLine("Deadline YYYY-MM-DD HH:MM (blank for none): ");

    if (db.addTask(t)) std::cout << "Task added.\n";
    else               std::cout << "Could not add task.\n";
}

void markComplete(Database& db)
{
    listTasks(db, true);
    int id = readInt("\nId of the task to mark complete (0 to cancel): ", 0, 1000000);
    if (id == 0) return;

    if (db.markComplete(id)) std::cout << "Marked complete.\n";
}

// The central feature. Reads pending tasks, compresses them to fit the
// time available, shows the result, and offers to save it.
void planDay(Database& db)
{
    std::vector<Task> tasks = db.getPendingTasks();
    if (tasks.empty()) {
        std::cout << "\nNothing pending. Nothing to plan.\n";
        return;
    }

    // Convert database rows into the plain structs the scheduler accepts.
    // The scheduler knows nothing about SQLite; this is where the two
    // layers meet.
    std::vector<ShrinkItem> items;
    int totalWanted = 0, totalFloor = 0;

    for (const Task& t : tasks) {
        ShrinkItem s;
        s.id        = t.id;
        s.title     = t.title;
        s.requested = t.estimatedTime;
        s.minimum   = t.minTime;
        s.priority  = t.priority;
        s.mandatory = t.isMandatory;
        items.push_back(s);

        totalWanted += t.estimatedTime;
        totalFloor  += t.minTime;
    }

    // The window runs from now until you next wake up. Sleep is one of the
    // tasks INSIDE that window, so if sleep gets compressed, the result is
    // a later bedtime -- which is exactly the original design.
    int wake   = getWakeTime(db);
    int budget = minutesUntil(wake);

    std::cout << "\nIt is now " << formatHM(nowMinutes())
              << ". You wake up at " << formatHM(wake) << ".\n";
    std::cout << "That leaves " << budget << " minutes ("
              << budget / 60 << "h " << budget % 60 << "m), including sleep.\n";

    std::cout << "\n" << tasks.size() << " pending task(s) want "
              << totalWanted << " minutes.\n";
    std::cout << "Compressed to their minimums they would need "
              << totalFloor << " minutes.\n";

    PlanResult plan = planWithDropping(items, budget);

    // The kept list is a subset of `items`, so look priority up by id
    // rather than by position.
    auto priorityOf = [&items](int id) {
        for (const ShrinkItem& s : items) if (s.id == id) return s.priority;
        return 0;
    };

    std::cout << "\n" << std::left
              << std::setw(5)  << "id"
              << std::setw(22) << "title"
              << std::right
              << std::setw(5)  << "pri"
              << std::setw(9)  << "wanted"
              << std::setw(9)  << "planned"
              << std::setw(7)  << "lost" << "\n";
    std::cout << std::string(57, '-') << "\n";

    int total = 0;
    for (const Allocation& a : plan.kept.allocations) {
        std::cout << std::left
                  << std::setw(5)  << a.id
                  << std::setw(22) << a.title.substr(0, 21)
                  << std::right
                  << std::setw(5)  << priorityOf(a.id)
                  << std::setw(9)  << a.requested
                  << std::setw(9)  << a.allocated
                  << std::setw(7)  << a.lost() << "\n";
        total += a.allocated;
    }
    std::cout << std::string(57, '-') << "\n";
    std::cout << "Total planned: " << total << " / " << budget << " minutes\n";

    if (!plan.dropped.empty()) {
        std::cout << "\nDropped to make the day fit (move these to another day):\n";
        for (const Allocation& a : plan.dropped) {
            std::cout << "  - " << a.title << "  (priority " << priorityOf(a.id)
                      << ", " << a.requested << " min)\n";
        }
    }

    if (plan.kept.deficit > 0) {
        std::cout << "\nThe remaining tasks were shortened by "
                  << plan.kept.deficit << " minutes in total.\n";
    } else if (plan.dropped.empty()) {
        std::cout << "\nEverything fits at full length.\n";
    }

    if (!plan.feasible) {
        std::cout << "\nTHIS DAY STILL DOES NOT FIT. Even your mandatory tasks, "
                  << "at their minimums, are " << plan.shortfall
                  << " minutes over.\n"
                  << "Nothing optional is left to drop. Consider marking a task "
                  << "as not mandatory, or moving it to another day.\n";
        return;
    }

    if (readYesNo("\nSave this plan? (y/n): ")) {
        std::vector<PlanRow> rows;
        for (const Allocation& a : plan.kept.allocations) {
            PlanRow p;
            p.taskId    = a.id;
            p.requested = a.requested;
            p.allocated = a.allocated;
            rows.push_back(p);
        }
        // Dropped tasks are saved with 0 minutes, so the history shows
        // what was given up on that day, not just what was done.
        for (const Allocation& a : plan.dropped) {
            PlanRow p;
            p.taskId    = a.id;
            p.requested = a.requested;
            p.allocated = 0;
            rows.push_back(p);
        }

        std::string date = todayString();
        if (db.savePlan(date, rows))
            std::cout << "Plan saved for " << date << ".\n";
        else
            std::cout << "Could not save the plan.\n";
    }
}

// "2026-10-07 18:00" -> minutes from now until then (negative if past).
// A bare date "2026-10-07" is treated as end of that day.
// Returns false if the text is not a date we can read.
bool minutesUntilDeadline(const std::string& s, long& out)
{
    int y = 0, mo = 0, d = 0, h = 23, mi = 59;
    int n = std::sscanf(s.c_str(), "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi);
    if (n != 3 && n != 5) return false;
    if (n == 3) { h = 23; mi = 59; }

    std::tm tm{};
    tm.tm_year  = y - 1900;
    tm.tm_mon   = mo - 1;
    tm.tm_mday  = d;
    tm.tm_hour  = h;
    tm.tm_min   = mi;
    tm.tm_isdst = -1;   // let the library work out daylight saving

    std::time_t deadline = std::mktime(&tm);
    if (deadline == static_cast<std::time_t>(-1)) return false;

    out = static_cast<long>(std::difftime(deadline, std::time(nullptr)) / 60);
    return true;
}

bool isSleepTask(const std::string& title)
{
    // Sleep is still stored as an ordinary task, so the only way to spot
    // it is by name. A proper routines table would remove this guess.
    std::string t;
    for (char c : title) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t == "sleep";
}

void whatNow(Database& db)
{
    std::vector<Task> tasks = db.getPendingTasks();
    if (tasks.empty()) {
        std::cout << "\nNothing pending. You're free.\n";
        return;
    }

    int wake = getWakeTime(db);

    std::cout << "\nThe clock says " << formatHM(nowMinutes())
              << ", but only you know how you feel.\n";
    int energy = readInt("Your energy right now, 1 (drained) to 5 (sharp): ", 1, 5);

    std::vector<AdviceItem> items;
    for (const Task& t : tasks) {
        AdviceItem a;
        a.id             = t.id;
        a.title          = t.title;
        a.priority       = t.priority;
        a.energyRequired = t.energyRequired;
        a.estimatedTime  = t.estimatedTime;
        a.isMandatory    = t.isMandatory;
        a.isSleep        = isSleepTask(t.title);

        if (!t.deadline.empty()) {
            a.hasDeadline = minutesUntilDeadline(t.deadline, a.minutesToDeadline);
        }
        items.push_back(a);
    }

    AdviceContext ctx;
    ctx.userEnergy  = energy;
    ctx.nowMinutes  = nowMinutes();
    ctx.wakeMinutes = wake;

    std::vector<Suggestion> picks = suggestNext(items, ctx, 3);

    if (picks.empty()) {
        std::cout << "\nNothing suitable right now.\n";
        return;
    }

    auto printReasons = [](const Suggestion& s) {
        std::cout << "     because: ";
        for (std::size_t i = 0; i < s.reasons.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << s.reasons[i];
        }
        std::cout << "\n";
    };

    std::cout << "\n>>> Do this now: " << picks[0].title
              << "  (about " << picks[0].estimatedTime << " min)\n";
    printReasons(picks[0]);

    if (picks.size() > 1) {
        std::cout << "\nAfter that:\n";
        for (std::size_t i = 1; i < picks.size(); ++i) {
            std::cout << "  " << i + 1 << ". " << picks[i].title
                      << "  (about " << picks[i].estimatedTime << " min)\n";
            printReasons(picks[i]);
        }
    }
}

void deleteTask(Database& db)
{
    listTasks(db, false);
    int id = readInt("\nId of the task to delete (0 to cancel): ", 0, 1000000);
    if (id == 0) return;

    if (!readYesNo("Delete task " + std::to_string(id) + " permanently? (y/n): ")) return;

    if (db.deleteTask(id)) std::cout << "Deleted.\n";
}

void changeWakeTime(Database& db)
{
    std::string current;
    if (db.getSetting("wake_time", current))
        std::cout << "\nCurrent wake-up time: " << current << "\n";
    else
        std::cout << "\nNo wake-up time saved yet.\n";

    std::string t = readTime("New wake-up time (HH:MM): ");
    if (db.setSetting("wake_time", t))
        std::cout << "Wake-up time set to " << t << ".\n";
}

} // namespace


int main()
{
    Database db("tasks.db");
    if (!db.isOpen()) {
        std::cout << "Database could not be opened. Exiting.\n";
        return 1;
    }

    // Safe on every run; creates the tables only if they are missing.
    db.createTable();
    db.createDayPlanTable();
    db.createSettingsTable();

    std::cout << "==============================\n"
              << "        PrioriTask\n"
              << "==============================\n"
              << "Today is " << todayString()
              << ", " << formatHM(nowMinutes()) << "\n";

    bool running = true;
    while (running) {
        std::cout << "\n------------------------------\n"
                  << " 1. What should I do now?\n"
                  << " 2. Plan my day\n"
                  << " 3. List all tasks\n"
                  << " 4. List pending tasks\n"
                  << " 5. Add a task\n"
                  << " 6. Mark a task complete\n"
                  << " 7. Delete a task\n"
                  << " 8. Change wake-up time\n"
                  << " 9. Quit\n"
                  << "------------------------------\n";

        switch (readInt("Choose 1-9: ", 1, 9)) {
            case 1: whatNow(db);          break;
            case 2: planDay(db);          break;
            case 3: listTasks(db, false); break;
            case 4: listTasks(db, true);  break;
            case 5: addTask(db);          break;
            case 6: markComplete(db);     break;
            case 7: deleteTask(db);       break;
            case 8: changeWakeTime(db);   break;
            case 9: running = false;      break;
        }
    }

    std::cout << "Goodbye.\n";
    return 0;
}