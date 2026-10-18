#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "Book/InnerBook/LevelSummary.h"
#include "Book/InnerBook/MarketMaker/OrderIndex.h"
#include "Book/InnerBook/Order.h"

template <typename T>
concept LevelSide = std::default_initializable<T> &&
                    requires(T side, const T& view, Price price, uint32_t quantity, int64_t change, std::size_t levels) {
                        side.add(price, quantity);
                        side.remove(price, quantity);
                        side.adjust(price, change);
                        side.clear();
                        { view.size() } -> std::convertible_to<std::size_t>;
                        { view.empty() } -> std::convertible_to<bool>;
                        { view.best() } -> std::same_as<std::optional<Price>>;
                        { view.depth(levels) } -> std::same_as<std::vector<LevelSummary>>;
                    };

template <typename Levels>
using BidSide = typename Levels::template Side<std::greater<>>;

template <typename Levels>
using AskSide = typename Levels::template Side<std::less<>>;

template <typename Levels>
concept LevelPolicy = LevelSide<BidSide<Levels>> && LevelSide<AskSide<Levels>>;

template <typename Index>
concept OrderIndex = requires(Index index, uint64_t id, RestingOrder order) {
    index.insert(id, order);
    index.get(index.find(id));
    index.erase(index.find(id));
};
