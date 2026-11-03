#ifndef DSA_H
#define DSA_H

// ---------------------------------------------------------------
// PrioriTask's own data structures and algorithms.
//
// Everything the project used to take from the STL is written here
// instead:
//   DynArray<T>          growable array          (was std::vector)
//   BinaryHeap<T, Less>  array-based binary heap (was std::priority_queue)
//   mergeSort(arr, less) stable O(n log n) sort  (was std::sort)
//   minOf, maxOf, clampTo                        (were std::min/max/clamp)
//
// These are templates, so they live entirely in this header: the
// compiler needs to see the code to build a version for each type.
// ---------------------------------------------------------------

#include <cstddef>   // std::size_t, the type used for sizes and indexes

// ===============================================================
// Small helpers
// ===============================================================
template <class T>
const T& minOf(const T& a, const T& b) { return (b < a) ? b : a; }

template <class T>
const T& maxOf(const T& a, const T& b) { return (a < b) ? b : a; }

// lo if v < lo, hi if v > hi, otherwise v.
template <class T>
const T& clampTo(const T& v, const T& lo, const T& hi)
{
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

template <class T>
void swapValues(T& a, T& b)
{
    T tmp = a;
    a = b;
    b = tmp;
}

// ===============================================================
// DynArray: a growable array
//
// Elements sit side by side in one block of memory, so element i is
// found directly, in O(1). When the block is full, a block twice the
// size is allocated and everything is copied across. Doubling means a
// copy happens rarely, so push_back is O(1) on average ("amortised").
// ===============================================================
template <class T>
class DynArray {
    T*          data_     = nullptr;
    std::size_t size_     = 0;   // elements in use
    std::size_t capacity_ = 0;   // elements the block can hold

    // Moves to a block of exactly newCapacity elements.
    void reallocate(std::size_t newCapacity)
    {
        T* fresh = new T[newCapacity];
        for (std::size_t i = 0; i < size_; ++i) fresh[i] = data_[i];
        delete[] data_;
        data_     = fresh;
        capacity_ = newCapacity;
    }

    void growIfFull()
    {
        if (size_ == capacity_) reallocate(capacity_ == 0 ? 4 : capacity_ * 2);
    }

public:
    DynArray() = default;

    // n default-valued elements, e.g. DynArray<std::string> names(5).
    explicit DynArray(std::size_t n) : DynArray(n, T()) {}

    // n copies of value, e.g. DynArray<bool> keep(10, true).
    DynArray(std::size_t n, const T& value)
    {
        if (n == 0) return;
        data_     = new T[n];
        size_     = n;
        capacity_ = n;
        for (std::size_t i = 0; i < n; ++i) data_[i] = value;
    }

    // Copying makes an independent copy of every element.
    DynArray(const DynArray& other)
    {
        if (other.size_ == 0) return;
        data_     = new T[other.size_];
        size_     = other.size_;
        capacity_ = other.size_;
        for (std::size_t i = 0; i < size_; ++i) data_[i] = other.data_[i];
    }

    DynArray& operator=(const DynArray& other)
    {
        if (this != &other) {
            DynArray copy(other);   // copy first, so a failure leaves *this unchanged
            swapWith(copy);
        }
        return *this;
    }

    // Moving takes over the other array's memory instead of copying it.
    DynArray(DynArray&& other) noexcept { swapWith(other); }

    DynArray& operator=(DynArray&& other) noexcept
    {
        if (this != &other) {
            clear();
            delete[] data_;
            data_ = nullptr;
            capacity_ = 0;
            swapWith(other);
        }
        return *this;
    }

    ~DynArray() { delete[] data_; }

    void swapWith(DynArray& other) noexcept
    {
        T* d = data_;              data_ = other.data_;         other.data_ = d;
        std::size_t s = size_;     size_ = other.size_;         other.size_ = s;
        std::size_t c = capacity_; capacity_ = other.capacity_; other.capacity_ = c;
    }

    // ---- Adding and removing ----
    void push_back(const T& value)
    {
        if (size_ == capacity_) {
            T copy = value;        // value may live inside this array; copy it before moving
            growIfFull();
            data_[size_++] = copy;
            return;
        }
        data_[size_++] = value;
    }

    void pop_back() { if (size_ > 0) --size_; }

    void clear() { size_ = 0; }

    // Inserts value before position pos, shifting later elements right. O(n).
    T* insert(T* pos, const T& value)
    {
        std::size_t index = static_cast<std::size_t>(pos - data_);   // pos may move when we grow
        T copy = value;
        growIfFull();
        for (std::size_t i = size_; i > index; --i) data_[i] = data_[i - 1];
        data_[index] = copy;
        ++size_;
        return data_ + index;
    }

    // ---- Access ----
    std::size_t size()  const { return size_; }
    bool        empty() const { return size_ == 0; }

    T&       operator[](std::size_t i)       { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }

    T&       back()       { return data_[size_ - 1]; }
    const T& back() const { return data_[size_ - 1]; }

    // begin/end let "for (x : array)" loops work.
    T*       begin()       { return data_; }
    T*       end()         { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end()   const { return data_ + size_; }
};

// ===============================================================
// BinaryHeap: a priority queue stored in an array
//
// The array is read as a complete binary tree: the node at index i has
// children at 2i+1 and 2i+2, and its parent at (i-1)/2. Every parent
// ranks at least as high as its children, so the best element is always
// at index 0.
//
// less(a, b) returns true when a ranks LOWER than b. The element that
// ranks highest comes out first -- the same rule std::priority_queue
// uses, so a comparator written for one works for the other.
//
//   top()  O(1)
//   push() O(log n): add at the end, sift up
//   pop()  O(log n): move the last element to the root, sift down
// ===============================================================
template <class T, class Less>
class BinaryHeap {
    DynArray<T> items_;
    Less        less_;

    // A new element at the bottom rises while it outranks its parent.
    void siftUp(std::size_t i)
    {
        while (i > 0) {
            std::size_t parent = (i - 1) / 2;
            if (!less_(items_[parent], items_[i])) break;
            swapValues(items_[parent], items_[i]);
            i = parent;
        }
    }

    // An element at the top sinks while a child outranks it, always
    // swapping with the higher-ranking child.
    void siftDown(std::size_t i)
    {
        std::size_t n = items_.size();
        while (true) {
            std::size_t left  = 2 * i + 1;
            std::size_t right = 2 * i + 2;
            std::size_t best  = i;
            if (left  < n && less_(items_[best], items_[left]))  best = left;
            if (right < n && less_(items_[best], items_[right])) best = right;
            if (best == i) break;
            swapValues(items_[i], items_[best]);
            i = best;
        }
    }

public:
    void push(const T& value)
    {
        items_.push_back(value);
        siftUp(items_.size() - 1);
    }

    const T& top() const { return items_[0]; }

    void pop()
    {
        if (items_.empty()) return;
        items_[0] = items_.back();
        items_.pop_back();
        if (!items_.empty()) siftDown(0);
    }

    std::size_t size()  const { return items_.size(); }
    bool        empty() const { return items_.empty(); }
};

// ===============================================================
// mergeSort: stable O(n log n) sort
//
// Split the range in half, sort each half, then merge the two sorted
// halves by repeatedly taking the smaller front element. "Stable"
// means equal elements keep their original order.
//
// less(a, b) returns true when a should come before b.
// ===============================================================
namespace dsa_detail {

template <class T, class Less>
void mergeSortRange(DynArray<T>& a, DynArray<T>& buffer,
                    std::size_t lo, std::size_t hi, Less less)
{
    if (hi - lo < 2) return;                 // 0 or 1 element: already sorted

    std::size_t mid = lo + (hi - lo) / 2;
    mergeSortRange(a, buffer, lo, mid, less);
    mergeSortRange(a, buffer, mid, hi, less);

    std::size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        // Take from the right only if strictly smaller, so ties keep
        // their original order (stability).
        if (less(a[j], a[i])) buffer[k++] = a[j++];
        else                  buffer[k++] = a[i++];
    }
    while (i < mid) buffer[k++] = a[i++];
    while (j < hi)  buffer[k++] = a[j++];

    for (std::size_t t = lo; t < hi; ++t) a[t] = buffer[t];
}

} // namespace dsa_detail

template <class T, class Less>
void mergeSort(DynArray<T>& a, Less less)
{
    if (a.size() < 2) return;
    DynArray<T> buffer(a.size());
    dsa_detail::mergeSortRange(a, buffer, 0, a.size(), less);
}

#endif
