//
// Created by Stephen Linder on 8/31/26.
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

class Book;
class Order;
class SecurityBook;
class StockDirectory;

namespace Logger {

enum class Level {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3,
};

void set_min_level(Level level);
[[nodiscard]] Level min_level();

void set_output_stream(std::ostream& stream);

void error(const std::string& message);
void warn(const std::string& message);
void info(const std::string& message);
void debug(const std::string& message);

// --- Feed / decode (future threaded replay) ---
void log_feed_open(const std::string& path);
void log_feed_eof(const std::string& path, uint64_t frames_read);
void log_decode_skipped(char message_type, uint16_t stock_locate);
void log_queue_depth(std::size_t depth, std::size_t capacity);

// --- Stock directory ---
void log_directory_add(uint16_t stock_locate, const std::string& symbol, char market_category);
void log_directory_snapshot(const StockDirectory& directory);

// --- Order lifecycle (decoder / book apply) ---
void log_order_add(const Order& order);
void log_order_delete(uint16_t stock_locate, uint64_t order_reference_number);
void log_order_execute(uint16_t stock_locate,
                       uint64_t order_reference_number,
                       uint32_t executed_shares,
                       uint32_t quantity_remaining);
void log_order_cancel(uint16_t stock_locate,
                      uint64_t order_reference_number,
                      uint32_t cancelled_shares,
                      uint32_t quantity_remaining);
void log_order_replace(uint16_t stock_locate,
                       uint64_t old_order_reference_number,
                       uint64_t new_order_reference_number,
                       uint32_t new_price,
                       uint32_t new_shares);

// --- Book anomalies ---
void log_unknown_order(const char* action, uint16_t stock_locate, uint64_t order_reference_number);
void log_overfill(const char* action,
                  uint16_t stock_locate,
                  uint64_t order_reference_number,
                  uint32_t requested_shares,
                  uint32_t quantity_remaining);

// --- Book snapshots ---
void log_order(const Order& order);
void log_security_book_summary(const SecurityBook& book, const StockDirectory* directory);
void log_security_book_top(const SecurityBook& book, const StockDirectory* directory);
void log_security_book_depth(const SecurityBook& book, const StockDirectory* directory, std::size_t max_levels);
void log_book_summary(const Book& book);

}  // namespace Logger
