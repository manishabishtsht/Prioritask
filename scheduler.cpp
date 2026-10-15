#include "scheduler.h"

#include <algorithm>
#include <cstddef>

// ---------------------------------------------------------------
// Shared setup. Hands back the starting allocations and the totals,
// so both policies agree on what "fits" means.
// ---------------------------------------------------------------
namespace {

void prepare(const std::vector<ShrinkItem>& items,
             ShrinkResult& result,
             int& totalRequested,
             int& totalMinimum)
{
    totalRequested = 0;
    totalMinimum   = 0;

    for (const ShrinkItem& item : items) {
        Allocation a;
        a.id        = item.id;
        a.title     = item.title;
        a.requested = item.requested;
        a.allocated = item.requested;   // start by giving everyone what they want
        result.allocations.push_back(a);

        totalRequested += item.requested;
        totalMinimum   += item.minimum;
    }
}

// Returns true if the caller should stop early: either nothing needs
// cutting, or not even the floors will fit.
bool handleTrivialCases(const std::vector<ShrinkItem>& items,
                        ShrinkResult& result,
                        int totalRequested,
                        int totalMinimum,
                        int availableMinutes,
                        int& deficit)
{
    deficit = totalRequested - availableMinutes;

    // Everything already fits.
    if (deficit <= 0) {
        deficit = 0;
        return true;
    }
    result.deficit = deficit;

    // Even at every floor it will not fit. Shrink everything to its floor
    // and report how far over we are. Deciding what to DROP is a separate
    // problem and does not belong in a shrinking function.
    if (totalMinimum > availableMinutes) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            result.allocations[i].allocated = items[i].minimum;
        }
        result.feasible  = false;
        result.shortfall = totalMinimum - availableMinutes;
        return true;
    }

    return false;
}

// priority 5 -> weight 1 (protect), priority 1 -> weight 5 (cut first)
int weightOf(int priority)
{
    int w = 6 - priority;
    return (w < 1) ? 1 : w;
}

} // namespace


// ---------------------------------------------------------------
// POLICY A: equal split
// ---------------------------------------------------------------
ShrinkResult shrinkEqualSplit(const std::vector<ShrinkItem>& items,
                              int availableMinutes)
{
    ShrinkResult result;
    int totalRequested = 0, totalMinimum = 0, deficit = 0;

    prepare(items, result, totalRequested, totalMinimum);
    if (handleTrivialCases(items, result, totalRequested, totalMinimum,
                           availableMinutes, deficit)) {
        return result;
    }

    while (deficit > 0) {

        std::vector<std::size_t> flexible;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (result.allocations[i].allocated > items[i].minimum) {
                flexible.push_back(i);
            }
        }
        if (flexible.empty()) {
            result.feasible  = false;
            result.shortfall = deficit;
            break;
        }

        // Integer division can give 0 when the deficit is smaller than the
        // number of items. Force at least 1 so the loop always terminates.
        int share = deficit / static_cast<int>(flexible.size());
        if (share < 1) share = 1;

        for (std::size_t idx : flexible) {
            if (deficit == 0) break;

            int room = result.allocations[idx].allocated - items[idx].minimum;
            int cut  = std::min(share, std::min(room, deficit));

            result.allocations[idx].allocated -= cut;
            deficit -= cut;
        }
    }

    return result;
}


// ---------------------------------------------------------------
// POLICY B: weighted by priority
//
// Each pass, every flexible item gets a score of weight * slack.
// The remaining deficit is divided out in proportion to that score.
// Items that reach their floor drop out and the rest is redistributed
// on the next pass.
// ---------------------------------------------------------------
ShrinkResult shrinkWeighted(const std::vector<ShrinkItem>& items,
                            int availableMinutes)
{
    ShrinkResult result;
    int totalRequested = 0, totalMinimum = 0, deficit = 0;

    prepare(items, result, totalRequested, totalMinimum);
    if (handleTrivialCases(items, result, totalRequested, totalMinimum,
                           availableMinutes, deficit)) {
        return result;
    }

    while (deficit > 0) {

        std::vector<std::size_t> flexible;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (result.allocations[i].allocated > items[i].minimum) {
                flexible.push_back(i);
            }
        }
        if (flexible.empty()) {
            result.feasible  = false;
            result.shortfall = deficit;
            break;
        }

        // Score every flexible item. long long because deficit * score can
        // get large before the division.
        long long totalScore = 0;
        std::vector<long long> score(flexible.size(), 0);

        for (std::size_t k = 0; k < flexible.size(); ++k) {
            std::size_t idx = flexible[k];
            long long slack = result.allocations[idx].allocated - items[idx].minimum;
            score[k] = static_cast<long long>(weightOf(items[idx].priority)) * slack;
            totalScore += score[k];
        }
        if (totalScore == 0) break;   // cannot happen, but never loop forever

        // Snapshot the deficit so every item in this pass is measured
        // against the same number. Otherwise the result depends on the
        // order the items happen to sit in the vector.
        int passDeficit  = deficit;
        int cutThisPass  = 0;

        for (std::size_t k = 0; k < flexible.size(); ++k) {
            if (deficit == 0) break;
            std::size_t idx = flexible[k];

            int share = static_cast<int>(
                (static_cast<long long>(passDeficit) * score[k]) / totalScore);

            int room = result.allocations[idx].allocated - items[idx].minimum;
            int cut  = std::min(share, std::min(room, deficit));

            result.allocations[idx].allocated -= cut;
            deficit     -= cut;
            cutThisPass += cut;
        }

        // When the deficit is small, every share can truncate to zero and
        // the pass does nothing. Take one minute from the item with the
        // highest score so progress is guaranteed.
        if (cutThisPass == 0 && deficit > 0) {
            std::size_t best = 0;
            for (std::size_t k = 1; k < flexible.size(); ++k) {
                if (score[k] > score[best]) best = k;
            }
            result.allocations[flexible[best]].allocated -= 1;
            deficit -= 1;
        }
    }

    return result;
}
