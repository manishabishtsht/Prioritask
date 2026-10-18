#ifndef ADVISOR_H
#define ADVISOR_H

#include <cstddef>
#include <string>
#include <vector>

// ---------------------------------------------------------------
// Rule-based "what should I do now?" engine.
//
// Like the scheduler, this knows nothing about SQLite or the clock.
// main.cpp works out the times and passes plain numbers in, so every
// rule can be tested with fixed inputs.
// ---------------------------------------------------------------

// One task, as the advisor sees it.
struct AdviceItem {
    int         id                = 0;
    std::string title;
    int         priority          = 3;      // 1..5
    int         energyRequired    = 3;      // 1..5
    int         estimatedTime     = 0;      // minutes
    bool        isMandatory       = false;
    bool        hasDeadline       = false;
    long        minutesToDeadline = 0;      // negative means overdue
    bool        isSleep           = false;
    int         minimumTime       = 0;      // minutes; 0 = same as estimatedTime
    bool        needsOutside      = false;  // gym, shopping: needs leaving college or work
};

// Facts about right now.
struct AdviceContext {
    int userEnergy  = 0;     // 1..5, or 0 if unknown (energy rules skipped)
    int nowMinutes  = 0;     // minutes since midnight
    int wakeMinutes = 420;   // minutes since midnight

    // Free-slot rules (all optional; the defaults behave exactly as before)
    bool busyNow      = false;  // inside a fixed commitment (college, work) right now
    int  freeMinutes  = 0;      // a free slot the user has declared; 0 = none
    bool allowOutside = true;   // false = leave out tasks that need going out
};

// One recommendation, with the reasons that produced it.
struct Suggestion {
    int                      id            = 0;
    std::string              title;
    int                      score         = 0;
    int                      priority      = 0;
    int                      estimatedTime = 0;
    std::vector<std::string> reasons;
};

// Returns up to `howMany` suggestions, best first. Scoring only: no
// free-slot rules. Front ends should call adviseNow() instead.
std::vector<Suggestion> suggestNext(const std::vector<AdviceItem>& items,
                                    const AdviceContext& ctx,
                                    std::size_t howMany);

enum class AdviceStatus {
    Suggestions,    // here is what to do
    QuietBusy,      // in a commitment and no free slot declared: suggest nothing
    NothingFits     // nothing pending, or nothing fits the free slot
};

struct AdviceResult {
    AdviceStatus            status = AdviceStatus::Suggestions;
    std::vector<Suggestion> suggestions;
    int hiddenTooLong = 0;   // left out: even their minimum is longer than the slot
    int hiddenOutside = 0;   // left out: they need going out, and that wasn't allowed
};

// The full "What should I do now?" decision:
//  1. In a commitment with no free slot declared -> stay quiet.
//  2. A free slot declared -> only tasks whose minimum fits in it, no sleep,
//     and nothing that needs going out unless allowed. Each suggestion's
//     time is capped at the slot.
//  3. Otherwise -> the normal scored suggestions.
AdviceResult adviseNow(const std::vector<AdviceItem>& items,
                       const AdviceContext& ctx,
                       std::size_t howMany);

// "2d 8h", "3h 20m", "45m"
std::string formatDuration(long minutes);

#endif
