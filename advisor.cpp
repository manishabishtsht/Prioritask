#include "advisor.h"

#include <cstdio>
#include <cstdlib>
#include <queue>
#include <string>

std::string formatDuration(long minutes)
{
    if (minutes < 0) minutes = -minutes;

    long days  = minutes / (24 * 60);
    long hours = (minutes % (24 * 60)) / 60;
    long mins  = minutes % 60;

    if (days  > 0) return std::to_string(days)  + "d " + std::to_string(hours) + "h";
    if (hours > 0) return std::to_string(hours) + "h " + std::to_string(mins)  + "m";
    return std::to_string(mins) + "m";
}

namespace {

// True if `now` lies in [start, end) on a 24-hour clock. Handles windows
// that cross midnight, such as 23:00 -> 07:00.
bool inWindow(int start, int end, int now)
{
    if (start <= end) return now >= start && now < end;
    return now >= start || now < end;
}

// Orders the priority queue. Highest score first; ties go to higher
// priority, then to the shorter task (a quick win), then to the lower id
// so the result is always deterministic.
struct WorseThan {
    bool operator()(const Suggestion& a, const Suggestion& b) const {
        if (a.score         != b.score)         return a.score         < b.score;
        if (a.priority      != b.priority)      return a.priority      < b.priority;
        if (a.estimatedTime != b.estimatedTime) return a.estimatedTime > b.estimatedTime;
        return a.id > b.id;
    }
};

} // namespace


std::vector<Suggestion> suggestNext(const std::vector<AdviceItem>& items,
                                    const AdviceContext& ctx,
                                    std::size_t howMany)
{
    // A max-heap: the top is always the best remaining suggestion.
    std::priority_queue<Suggestion, std::vector<Suggestion>, WorseThan> heap;

    for (const AdviceItem& it : items) {
        Suggestion s;
        s.id            = it.id;
        s.title         = it.title;
        s.priority      = it.priority;
        s.estimatedTime = it.estimatedTime;

        // ---- Sleep is decided by the clock, not by score ----
        // Bedtime is the wake-up time minus how long you sleep. Sleep is
        // only suggested from 30 minutes before bedtime until wake-up;
        // at any other time it is left out entirely, otherwise its
        // priority of 5 would make it the answer at 8 in the morning.
        if (it.isSleep) {
            int bedtime   = ((ctx.wakeMinutes - it.estimatedTime) % 1440 + 1440) % 1440;
            int windowBeg = ((bedtime - 30) % 1440 + 1440) % 1440;

            if (!inWindow(windowBeg, ctx.wakeMinutes, ctx.nowMinutes)) continue;

            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02d:%02d", bedtime / 60, bedtime % 60);
            s.score = 1000;
            s.reasons.push_back(std::string("it's bedtime (") + buf + ")");
            heap.push(s);
            continue;
        }

        // ---- Urgency ----
        if (it.hasDeadline) {
            long m = it.minutesToDeadline;
            if (m < 0) {
                s.score += 100;
                s.reasons.push_back("overdue by " + formatDuration(m));
            } else if (m < 6 * 60) {
                s.score += 80;
                s.reasons.push_back("due in " + formatDuration(m));
            } else if (m < 24 * 60) {
                s.score += 60;
                s.reasons.push_back("due in " + formatDuration(m));
            } else if (m < 72 * 60) {
                s.score += 30;
                s.reasons.push_back("due in " + formatDuration(m));
            }
            // More than 3 days away: no urgency points.
        }

        // ---- Importance ----
        s.score += 10 * it.priority;
        s.reasons.push_back("priority " + std::to_string(it.priority));

        if (it.isMandatory) {
            s.score += 20;
            s.reasons.push_back("mandatory");
        }

        // ---- Energy ----
        // gap > 0 means the task needs more energy than you have.
        if (ctx.userEnergy > 0) {
            int gap = it.energyRequired - ctx.userEnergy;
            if (gap > 0) {
                s.score -= 15 * gap;
                s.reasons.push_back("needs energy " + std::to_string(it.energyRequired) +
                                    ", you're at " + std::to_string(ctx.userEnergy));
            } else if (std::abs(gap) <= 1) {
                s.score += 10;
                s.reasons.push_back("suits your energy");
            }
        }

        heap.push(s);
    }

    std::vector<Suggestion> result;
    while (!heap.empty() && result.size() < howMany) {
        result.push_back(heap.top());
        heap.pop();
    }
    return result;
}


// ---------------------------------------------------------------
// adviseNow: the free-slot rules wrapped around suggestNext
// ---------------------------------------------------------------
AdviceResult adviseNow(const std::vector<AdviceItem>& items,
                       const AdviceContext& ctx,
                       std::size_t howMany)
{
    AdviceResult out;
    bool slot = ctx.freeMinutes > 0;

    // Rule 1: during college or work, say nothing until the user asks with
    // a free slot. The app never interrupts a commitment on its own.
    if (ctx.busyNow && !slot) {
        out.status = AdviceStatus::QuietBusy;
        return out;
    }

    // Rule 2: filter to what can actually be done right now.
    std::vector<AdviceItem> doable;
    for (const AdviceItem& it : items) {
        if (it.needsOutside && !ctx.allowOutside) {
            ++out.hiddenOutside;
            continue;
        }
        if (slot) {
            if (it.isSleep) continue;   // a free period is not for sleeping

            // A task fits if its MINIMUM fits: a 90-minute task with a
            // 30-minute floor can still use a 60-minute gap.
            int floor = it.minimumTime > 0 ? it.minimumTime : it.estimatedTime;
            if (floor > ctx.freeMinutes) {
                ++out.hiddenTooLong;
                continue;
            }
        }
        doable.push_back(it);
    }

    out.suggestions = suggestNext(doable, ctx, howMany);

    // Inside a slot, cap each suggestion at the time available and say so.
    if (slot) {
        for (Suggestion& s : out.suggestions) {
            if (s.estimatedTime > ctx.freeMinutes) {
                s.reasons.insert(s.reasons.begin(),
                    "do " + std::to_string(ctx.freeMinutes) + " of its " +
                    std::to_string(s.estimatedTime) + " min");
                s.estimatedTime = ctx.freeMinutes;
            } else {
                s.reasons.insert(s.reasons.begin(),
                    "fits your " + std::to_string(ctx.freeMinutes) + "-minute slot");
            }
        }
    }

    out.status = out.suggestions.empty() ? AdviceStatus::NothingFits
                                         : AdviceStatus::Suggestions;
    return out;
}
