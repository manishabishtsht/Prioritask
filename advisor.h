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
};

// Facts about right now.
struct AdviceContext {
    int userEnergy  = 0;     // 1..5, or 0 if unknown (energy rules skipped)
    int nowMinutes  = 0;     // minutes since midnight
    int wakeMinutes = 420;   // minutes since midnight
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

// Returns up to `howMany` suggestions, best first.
std::vector<Suggestion> suggestNext(const std::vector<AdviceItem>& items,
                                    const AdviceContext& ctx,
                                    std::size_t howMany);

// "2d 8h", "3h 20m", "45m"
std::string formatDuration(long minutes);

#endif
