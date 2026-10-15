#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Book/Order.h"

enum class BookOpType : std::uint8_t {
    Add,
    Execute,
    Cancel,
    Delete,
    Replace,
};

inline constexpr std::size_t kBookOpTypes = 5;

constexpr std::string_view book_op_name(BookOpType type)
{
    constexpr std::string_view names[kBookOpTypes] = {"add", "execute", "cancel", "delete", "replace"};
    return names[static_cast<std::size_t>(type)];
}

struct BookOp {
    BookOpType type;
    Side side;
    std::uint32_t instrument;
    std::uint32_t quantity;
    std::uint64_t ref;
    std::uint64_t new_ref;
    Price price;
};

struct OrderFlow {
    std::vector<BookOp> ops;
    std::size_t warmup = 0;
    std::vector<Price> ticks;
    std::size_t peak_resting = 0;
};

struct FlowConfig {
    std::size_t ops = 2'000'000;
    std::uint32_t instruments = 1;
    std::size_t resting_per_instrument = 5'000;
    Price tick = 100;
    Price base_price = 1'500'000;
    double near_touch = 0.1;
    double move_probability = 0.02;
    double recent_cancel = 0.85;
    double far_fraction = 0.01;
    std::uint32_t far_ticks = 500;
    bool random_ids = false;
    std::uint64_t seed = 1;
};

OrderFlow generate_order_flow(const FlowConfig& config);

std::expected<OrderFlow, std::string> load_itch_order_flow(const std::filesystem::path& path);
