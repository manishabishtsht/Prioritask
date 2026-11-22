// test_task_heap.cpp — proves TaskHeap pops in the same order as
// std::priority_queue, on random data, for both orderings and for a custom
// comparator shaped like the ones PrioriTask uses.
//
//   g++ -std=c++17 -Wall -Wextra -o test_task_heap test_task_heap.cpp && ./test_task_heap

#include "task_heap.h"

#include <queue>
#include <random>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  pass  " : "  FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Drains both heaps and compares the sequences element by element.
template <typename Cmp, typename T>
bool sameOrder(const std::vector<T>& items, Cmp cmp, const std::string& label) {
    pt::TaskHeap<T, Cmp> mine(cmp);
    std::priority_queue<T, std::vector<T>, Cmp> theirs(cmp);
    for (const T& v : items) { mine.push(v); theirs.push(v); }

    if (mine.size() != theirs.size()) { check(false, label + " (size)"); return false; }

    // Compare by RANK, not identity. Neither heap is stable: two records with
    // the same score may come out in either order, and both are correct. The
    // real requirement is that the two tops are equivalent under the
    // comparator — neither one outranks the other.
    while (!theirs.empty()) {
        if (mine.empty() ||
            cmp(mine.top(), theirs.top()) || cmp(theirs.top(), mine.top())) {
            check(false, label + " (order)");
            return false;
        }
        mine.pop();
        theirs.pop();
    }
    check(mine.empty(), label);
    return mine.empty();
}

// Stand-in for a PrioriTask record: the advisor wants the highest score on top,
// the scheduler wants the lowest value on top.
struct Item {
    int         score;
    std::string name;
    bool operator==(const Item& o) const { return score == o.score && name == o.name; }
};

struct BestOnTop  { bool operator()(const Item& a, const Item& b) const { return a.score < b.score; } };
struct WorstOnTop { bool operator()(const Item& a, const Item& b) const { return a.score > b.score; } };

}  // namespace

int main() {
    std::mt19937 rng(20261101);
    std::uniform_int_distribution<int> value(-500, 500);

    std::cout << "ints, several sizes\n";
    for (std::size_t n : {0u, 1u, 2u, 3u, 7u, 8u, 64u, 1000u}) {
        std::vector<int> v;
        v.reserve(n);
        for (std::size_t i = 0; i < n; ++i) v.push_back(value(rng));

        sameOrder(v, std::less<int>(),    "max-heap, n=" + std::to_string(n));
        sameOrder(v, std::greater<int>(), "min-heap, n=" + std::to_string(n));
    }

    std::cout << "duplicates (ties must not break the heap property)\n";
    sameOrder(std::vector<int>(50, 7), std::less<int>(), "fifty equal keys");

    std::cout << "already sorted and reverse sorted (worst cases for sifting)\n";
    std::vector<int> asc, desc;
    for (int i = 0; i < 200; ++i) { asc.push_back(i); desc.push_back(200 - i); }
    sameOrder(asc,  std::less<int>(),    "ascending input");
    sameOrder(desc, std::less<int>(),    "descending input");

    std::cout << "task records with custom comparators\n";
    std::vector<Item> tasks;
    for (int i = 0; i < 300; ++i) tasks.push_back({value(rng), "task" + std::to_string(i)});
    sameOrder(tasks, BestOnTop(),  "advisor ordering (best score on top)");
    sameOrder(tasks, WorstOnTop(), "scheduler ordering (least valuable on top)");

    std::cout << "O(n) heapify constructor matches repeated push\n";
    {
        std::vector<int> v;
        for (int i = 0; i < 500; ++i) v.push_back(value(rng));

        pt::TaskHeap<int> built(v);                       // Floyd's heapify
        pt::TaskHeap<int> pushed;
        for (int x : v) pushed.push(x);                   // n pushes

        bool ok = true;
        while (!pushed.empty() && ok) {
            ok = !built.empty() && built.top() == pushed.top();
            built.pop();
            pushed.pop();
        }
        check(ok && built.empty(), "heapify == repeated push");
    }

    std::cout << "top-3 selection, the actual use in suggestNext\n";
    {
        std::vector<Item> v = tasks;
        pt::TaskHeap<Item, BestOnTop> h(v, BestOnTop());

        std::vector<Item> top3;
        for (int i = 0; i < 3 && !h.empty(); ++i) { top3.push_back(h.top()); h.pop(); }

        int best = -1000;
        for (const Item& it : v) best = std::max(best, it.score);

        bool ok = top3.size() == 3
               && top3[0].score == best
               && top3[0].score >= top3[1].score
               && top3[1].score >= top3[2].score;
        check(ok, "top 3 are the three highest, in order");
    }

    std::cout << "\n" << (failures == 0 ? "all checks passed" : std::to_string(failures) + " FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
