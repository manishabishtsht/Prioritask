#include "commitments.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {

const char* kDayShort[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
const char* kDayFull[7]  = {"monday", "tuesday", "wednesday", "thursday",
                            "friday", "saturday", "sunday"};

std::string lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s)
{
    std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// "mon", "Monday", "THU" -> 0..6. At least 3 letters, so "tu" (Tue or
// Thu?) is rejected rather than guessed. Returns -1 if not a day.
int dayIndex(const std::string& word)
{
    std::string w = lower(trim(word));
    if (w.size() < 3) return -1;
    for (int i = 0; i < 7; ++i) {
        std::string full = kDayFull[i];
        if (w.size() <= full.size() && full.compare(0, w.size(), w) == 0) return i;
    }
    return -1;
}

struct Interval {
    int         start;   // minutes, relative to today's midnight
    int         end;
    std::string label;
};

// Every occurrence of every commitment on yesterday, today and tomorrow.
// Yesterday is needed because a night shift that started yesterday may
// still be running now.
std::vector<Interval> occurrences(const std::vector<CommitmentBlock>& blocks, int todayIdx)
{
    std::vector<Interval> out;
    for (int d = -1; d <= 1; ++d) {
        int weekday = ((todayIdx + d) % 7 + 7) % 7;
        for (const CommitmentBlock& b : blocks) {
            if (b.days.size() != 7 || b.days[weekday] != '1') continue;
            int s = d * 1440 + b.startMin;
            int e = d * 1440 + (b.endMin > b.startMin ? b.endMin : b.endMin + 1440);
            out.push_back({s, e, b.label});
        }
    }
    return out;
}

// Merge overlapping intervals: sort by start time, then sweep left to
// right, extending the current block whenever the next one overlaps or
// touches it. O(n log n). This is what stops a 9-4 lecture block and a
// 2-5 lab being counted as 10 hours of busy time instead of 8.
std::vector<Interval> mergeIntervals(std::vector<Interval> v)
{
    std::sort(v.begin(), v.end(),
              [](const Interval& a, const Interval& b) { return a.start < b.start; });

    std::vector<Interval> merged;
    for (const Interval& iv : v) {
        if (!merged.empty() && iv.start <= merged.back().end) {
            merged.back().end = std::max(merged.back().end, iv.end);
        } else {
            merged.push_back(iv);
        }
    }
    return merged;
}

} // namespace


std::string parseDays(const std::string& text)
{
    std::string t = lower(trim(text));
    if (t == "daily" || t == "everyday" || t == "every day" || t == "all") return "1111111";
    if (t == "weekdays")                     return "1111100";
    if (t == "weekends" || t == "weekend")   return "0000011";

    std::string mask = "0000000";
    std::stringstream ss(t);
    std::string part;
    bool any = false;

    while (std::getline(ss, part, ',')) {
        part = trim(part);
        if (part.empty()) continue;

        std::size_t dash = part.find('-');
        if (dash == std::string::npos) {
            int d = dayIndex(part);
            if (d < 0) return "";
            mask[d] = '1';
        } else {
            int a = dayIndex(part.substr(0, dash));
            int b = dayIndex(part.substr(dash + 1));
            if (a < 0 || b < 0) return "";
            // Walk forward from a to b, wrapping round the week, so that
            // "fri-mon" means Fri, Sat, Sun, Mon.
            for (int d = a; ; d = (d + 1) % 7) {
                mask[d] = '1';
                if (d == b) break;
            }
        }
        any = true;
    }
    return any ? mask : "";
}

std::string daysToText(const std::string& mask)
{
    if (mask.size() != 7)  return "?";
    if (mask == "1111111") return "Every day";

    std::size_t first = mask.find('1');
    if (first == std::string::npos) return "No days";
    std::size_t last = mask.rfind('1');

    // One unbroken run of three or more days reads best as a range.
    std::string run = mask.substr(first, last - first + 1);
    if (run.find('0') == std::string::npos && run.size() >= 3) {
        return std::string(kDayShort[first]) + "-" + kDayShort[last];
    }

    std::string out;
    for (int i = 0; i < 7; ++i) {
        if (mask[i] != '1') continue;
        if (!out.empty()) out += ", ";
        out += kDayShort[i];
    }
    return out;
}

int busyMinutesInWindow(const std::vector<CommitmentBlock>& blocks,
                        int nowMin, int windowLen, int todayIdx)
{
    int wStart = nowMin;
    int wEnd   = nowMin + windowLen;

    // Clip every occurrence to the window, then merge so overlaps are
    // counted once.
    std::vector<Interval> clipped;
    for (const Interval& iv : occurrences(blocks, todayIdx)) {
        int s = std::max(iv.start, wStart);
        int e = std::min(iv.end,   wEnd);
        if (e > s) clipped.push_back({s, e, iv.label});
    }

    int total = 0;
    for (const Interval& iv : mergeIntervals(clipped)) total += iv.end - iv.start;
    return total;
}

bool currentCommitment(const std::vector<CommitmentBlock>& blocks,
                       int nowMin, int todayIdx,
                       std::string& label, int& freeAtMin, int& minutesLeft)
{
    std::vector<Interval> occ = occurrences(blocks, todayIdx);

    // Which commitment is running right now? If several, the one ending last.
    const Interval* hit = nullptr;
    for (const Interval& iv : occ) {
        if (iv.start <= nowMin && nowMin < iv.end && (!hit || iv.end > hit->end)) hit = &iv;
    }
    if (!hit) return false;

    // Back-to-back commitments (College until 16:00, Tuition 16:00-17:00)
    // mean you're only free when the whole merged block ends.
    int freeAt = hit->end;
    for (const Interval& m : mergeIntervals(occ)) {
        if (m.start <= nowMin && nowMin < m.end) { freeAt = m.end; break; }
    }

    label       = hit->label;
    minutesLeft = freeAt - nowMin;
    freeAtMin   = ((freeAt % 1440) + 1440) % 1440;
    return true;
}