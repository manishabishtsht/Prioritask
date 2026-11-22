// task_heap.h — binary heap implemented for PrioriTask.
//
// Replaces std::priority_queue at two call sites:
//   scheduler.cpp : planWithDropping  (min-ordered — least valuable task on top)
//   advisor.cpp   : suggestNext       (max-ordered — best-scoring task on top)
//
// The heap logic (siftUp, siftDown, build) is ours. std::vector is used only as
// the backing array, because a binary heap IS an array with an index rule:
//
//        index i  ->  parent (i-1)/2,  children 2i+1 and 2i+2
//
// Comparator convention matches std::priority_queue exactly, so the swap is a
// type change and nothing else: comp(a, b) == true means "a comes out AFTER b",
// i.e. the element that is never less-than any other ends up at the top.
//   std::less<T>    -> max-heap
//   std::greater<T> -> min-heap
//   custom functor  -> whatever ordering you define

#ifndef PRIORITASK_TASK_HEAP_H
#define PRIORITASK_TASK_HEAP_H

#include <vector>
#include <functional>   // std::less
#include <utility>      // std::swap, std::move
#include <cstddef>      // std::size_t

namespace pt {

template <typename T, typename Compare = std::less<T>>
class TaskHeap {
public:
    TaskHeap() = default;

    explicit TaskHeap(Compare cmp) : comp_(cmp) {}

    // Build from an existing array in O(n) — Floyd's heapify.
    // Cheaper than n separate pushes (O(n log n)); used where every element is
    // known up front, which is the case in suggestNext.
    TaskHeap(std::vector<T> items, Compare cmp = Compare())
        : data_(std::move(items)), comp_(cmp) {
        build();
    }

    bool        empty() const { return data_.empty(); }
    std::size_t size()  const { return data_.size(); }

    // Highest-ranked element under the comparator. Undefined if empty —
    // same contract as std::priority_queue::top.
    const T& top() const { return data_[0]; }

    // Append at the end, then walk it up to its place. O(log n).
    void push(const T& value) {
        data_.push_back(value);
        siftUp(data_.size() - 1);
    }

    void push(T&& value) {
        data_.push_back(std::move(value));
        siftUp(data_.size() - 1);
    }

    // Move the last element into the root, shrink, then walk it down. O(log n).
    void pop() {
        if (data_.empty()) return;
        data_[0] = std::move(data_.back());
        data_.pop_back();
        if (!data_.empty()) siftDown(0);
    }

    void clear() { data_.clear(); }

private:
    std::vector<T> data_;
    Compare        comp_{};

    // A node outranks its parent -> swap and keep climbing.
    void siftUp(std::size_t i) {
        while (i > 0) {
            std::size_t parent = (i - 1) / 2;
            if (!comp_(data_[parent], data_[i])) break;   // parent already wins
            std::swap(data_[parent], data_[i]);
            i = parent;
        }
    }

    // A child outranks the node -> swap with the stronger child, keep sinking.
    void siftDown(std::size_t i) {
        const std::size_t n = data_.size();
        for (;;) {
            std::size_t left  = 2 * i + 1;
            std::size_t right = 2 * i + 2;
            std::size_t best  = i;

            if (left  < n && comp_(data_[best], data_[left]))  best = left;
            if (right < n && comp_(data_[best], data_[right])) best = right;

            if (best == i) break;                          // heap property holds
            std::swap(data_[i], data_[best]);
            i = best;
        }
    }

    // Floyd's heapify: sift down every internal node, last to first. O(n).
    // Leaves (the back half of the array) are already valid one-element heaps.
    void build() {
        if (data_.size() < 2) return;
        for (std::size_t i = data_.size() / 2; i-- > 0; ) siftDown(i);
    }
};

}  // namespace pt

#endif  // PRIORITASK_TASK_HEAP_H
