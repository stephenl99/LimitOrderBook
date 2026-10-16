#pragma once

#include <cstddef>
#include <deque>
#include <iterator>
#include <memory_resource>
#include <vector>

#include "Level.h"
#include "Book/InnerBook/LevelSearch.h"
#include "Book/InnerBook/LevelSummary.h"

template <typename Better, std::size_t LinearProbes = kLinearOnly>
class FlatLevelMap {
    struct Entry {
        Price price;
        Level* level;
    };

    using Entries = std::vector<Entry>;

public:
    struct LevelView {
        Price first;
        const Level& second;
    };

    class Iterator {
    public:
        using value_type = LevelView;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;

        Iterator() = default;
        explicit Iterator(typename Entries::const_reverse_iterator position) : position_(position) {}

        LevelView operator*() const { return {position_->price, *position_->level}; }

        Iterator& operator++()
        {
            ++position_;
            return *this;
        }

        Iterator operator++(int)
        {
            Iterator previous = *this;
            ++position_;
            return previous;
        }

        bool operator==(const Iterator&) const = default;

    private:
        typename Entries::const_reverse_iterator position_;
    };

    Level& level_at(Price price, std::pmr::memory_resource* memory)
    {
        const auto [index, found] = locate_level<Better, LinearProbes>(entries_, price);
        if (found) {
            return *entries_[index].level;
        }
        Level* level = acquire(memory);
        entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(index), Entry{price, level});
        return *level;
    }

    void erase(Price price)
    {
        const auto [index, found] = locate_level<Better, LinearProbes>(entries_, price);
        if (!found) {
            return;
        }
        free_.push_back(entries_[index].level);
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    bool contains(Price price) const { return locate_level<Better, LinearProbes>(entries_, price).found; }

    std::size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

    void clear()
    {
        entries_.clear();
        free_.clear();
        storage_.clear();
    }

    Iterator begin() const { return Iterator(entries_.rbegin()); }
    Iterator end() const { return Iterator(entries_.rend()); }

private:
    Level* acquire(std::pmr::memory_resource* memory)
    {
        if (!free_.empty()) {
            Level* level = free_.back();
            free_.pop_back();
            return level;
        }
        return &storage_.emplace_back(memory);
    }

    Entries entries_;
    std::deque<Level> storage_;
    std::vector<Level*> free_;
};
