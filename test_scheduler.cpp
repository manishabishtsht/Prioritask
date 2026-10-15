// Compares the two shrinking policies on your six real tasks.
// Does NOT need SQLite to build.
//
//   g++ -std=c++17 -Wall test_scheduler.cpp scheduler.cpp -o test_scheduler.exe
//   .\test_scheduler.exe

#include "scheduler.h"

#include <iomanip>
#include <iostream>

static void compare(const std::vector<ShrinkItem>& items, int budget)
{
    ShrinkResult eq = shrinkEqualSplit(items, budget);
    ShrinkResult wt = shrinkWeighted(items, budget);

    std::cout << "\n=== Budget: " << budget << " minutes ===\n";
    std::cout << std::left  << std::setw(16) << "Task"
              << std::right << std::setw(5)  << "pri"
              << std::setw(8)  << "wants"
              << std::setw(9)  << "floor"
              << std::setw(11) << "EQUAL"
              << std::setw(11) << "WEIGHTED" << "\n";
    std::cout << std::string(60, '-') << "\n";

    int totalEq = 0, totalWt = 0;

    for (std::size_t i = 0; i < items.size(); ++i) {
        std::cout << std::left  << std::setw(16) << items[i].title
                  << std::right << std::setw(5)  << items[i].priority
                  << std::setw(8)  << items[i].requested
                  << std::setw(9)  << items[i].minimum
                  << std::setw(11) << eq.allocations[i].allocated
                  << std::setw(11) << wt.allocations[i].allocated << "\n";

        totalEq += eq.allocations[i].allocated;
        totalWt += wt.allocations[i].allocated;
    }

    std::cout << std::string(60, '-') << "\n";
    std::cout << std::left << std::setw(38) << "TOTAL"
              << std::right << std::setw(11) << totalEq
              << std::setw(11) << totalWt << "\n";

    if (!wt.feasible) {
        std::cout << "DOES NOT FIT. Still " << wt.shortfall
                  << " minutes over, even at every floor.\n";
        std::cout << "Shrinking cannot solve this. Something must be dropped.\n";
    }
}

int main()
{
    // Your actual tasks table. Last field is priority.
    std::vector<ShrinkItem> items = {
        {1, "DSA Assignment", 90,  60, 5},
        {2, "Gym",            60,  30, 3},
        {3, "Sleep",         450, 420, 5},
        {4, "Watch Lecture",  45,  30, 5},
        {5, "Read Book",      30,  15, 1},
        {6, "Revise Maths",   60,  30, 4}
    };
    // wants 735 minutes, floors sum to 585

    compare(items, 800);   // fits already
    compare(items, 700);   // mild squeeze  <-- the realistic case
    compare(items, 600);   // hard squeeze
    compare(items, 500);   // impossible

    return 0;
}
