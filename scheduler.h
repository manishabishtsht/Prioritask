#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <string>
#include <vector>

// What goes IN to a shrinking function.
// Deliberately does not mention SQLite or your Task struct, so the
// scheduler can be tested on its own.
struct ShrinkItem {
    int         id        = 0;
    std::string title;
    int         requested = 0;   // estimated_time: what it wants
    int         minimum   = 0;   // min_time: the floor it cannot go below
    int         priority  = 3;   // 1..5, 5 = most important
};

// What comes OUT, one per item.
struct Allocation {
    int         id        = 0;
    std::string title;
    int         requested = 0;
    int         allocated = 0;   // what it actually got
    int         lost() const { return requested - allocated; }
};

struct ShrinkResult {
    std::vector<Allocation> allocations;

    bool feasible  = true;  // false if even every floor together won't fit
    int  shortfall = 0;     // minutes still over budget when infeasible
    int  deficit   = 0;     // minutes that had to be removed in total
};

// POLICY A: equal split, capped by each item's floor.
// Every flexible item is asked for the same number of minutes.
// Ignores priority entirely.
ShrinkResult shrinkEqualSplit(const std::vector<ShrinkItem>& items,
                              int availableMinutes);

// POLICY B: weighted by priority, capped by each item's floor.
// An item's share of the cut is proportional to (weight * slack), where
// weight = 6 - priority. So a low-priority item with lots of room gives
// up the most; a high-priority item gives up least.
ShrinkResult shrinkWeighted(const std::vector<ShrinkItem>& items,
                            int availableMinutes);

#endif
