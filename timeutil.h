#ifndef TIMEUTIL_H
#define TIMEUTIL_H

// Clock and date helpers for the desktop front end. Header-only, so it
// adds nothing to the build command. main.cpp still has its own copies;
// switching it to this header is a later tidy-up.

#include <cctype>
#include <cstdio>
#include <ctime>
#include <string>

namespace tu {

inline std::tm localNow()
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

inline std::string todayString()
{
    std::tm tm = localNow();
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

inline int nowMinutes()
{
    std::tm tm = localNow();
    return tm.tm_hour * 60 + tm.tm_min;
}

// Monday = 0 ... Sunday = 6
inline int todayIndex()
{
    return (localNow().tm_wday + 6) % 7;
}

inline std::string formatHM(int minutes)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

// "07:00" -> 420, or -1 if not a valid HH:MM
inline int parseHM(const std::string& s)
{
    int h = 0, m = 0, used = 0;
    char colon = 0;
    if (std::sscanf(s.c_str(), "%d%c%d%n", &h, &colon, &m, &used) != 3) return -1;
    if (colon != ':' || used != static_cast<int>(s.size()))  return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59)                  return -1;
    return h * 60 + m;
}

// Minutes from now until the clock next reads wakeMinutes (never 0).
inline int minutesUntil(int wakeMinutes)
{
    int diff = wakeMinutes - nowMinutes();
    if (diff <= 0) diff += 24 * 60;
    return diff;
}

// "2026-10-20 18:00" or "2026-10-20", with a real calendar date.
inline bool isValidDeadline(const std::string& s)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, used = 0;
    int n = std::sscanf(s.c_str(), "%d-%d-%d %d:%d%n", &y, &mo, &d, &h, &mi, &used);
    if (n == 5) {
        if (used != static_cast<int>(s.size())) return false;
    } else {
        used = 0;
        n = std::sscanf(s.c_str(), "%d-%d-%d%n", &y, &mo, &d, &used);
        if (n != 3 || used != static_cast<int>(s.size())) return false;
        h = 23; mi = 59;
    }
    if (y < 2000 || y > 2100 || mo < 1 || mo > 12) return false;
    if (h < 0 || h > 23 || mi < 0 || mi > 59)       return false;

    static const int daysIn[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    int maxDay = daysIn[mo - 1] + ((mo == 2 && leap) ? 1 : 0);
    return d >= 1 && d <= maxDay;
}

// Minutes from now until a deadline (negative if past). False if unreadable.
inline bool minutesUntilDeadline(const std::string& s, long& out)
{
    int y = 0, mo = 0, d = 0, h = 23, mi = 59;
    int n = std::sscanf(s.c_str(), "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi);
    if (n != 3 && n != 5) return false;
    if (n == 3) { h = 23; mi = 59; }

    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
    tm.tm_hour = h;        tm.tm_min = mi;     tm.tm_isdst = -1;

    std::time_t when = std::mktime(&tm);
    if (when == static_cast<std::time_t>(-1)) return false;
    out = static_cast<long>(std::difftime(when, std::time(nullptr)) / 60);
    return true;
}

// Sleep is still an ordinary task, so it is recognised by its name.
inline bool isSleepTask(const std::string& title)
{
    std::string t;
    for (char c : title) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t == "sleep";
}

} // namespace tu

#endif
