#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "Book/InnerBook/Order.h"
#include "Book/InnerBook/Exchange/SecurityBook.h"
#include "Book/OuterBook/ItchBook.h"
#include "Book/OuterBook/ItchSecurityDirectory.h"

namespace ItchLog {

void log_feed_open(const std::string& path);
void log_feed_eof(const std::string& path, uint64_t frames_read);
void log_decode_skipped(char message_type, uint16_t stock_locate);
void log_queue_depth(std::size_t depth, std::size_t capacity);

void log_directory_add(uint16_t stock_locate, const std::string& symbol, char market_category);
void log_directory_snapshot(const ItchSecurityDirectory& directory);

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

void log_unknown_order(const char* action, uint16_t stock_locate, uint64_t order_reference_number);
void log_overfill(const char* action,
                  uint16_t stock_locate,
                  uint64_t order_reference_number,
                  uint32_t requested_shares,
                  uint32_t quantity_remaining);

void log_order(const Order& order);
void log_security_book_summary(const SecurityBook& book, const ItchSecurityDirectory* directory);
void log_security_book_top(const SecurityBook& book, const ItchSecurityDirectory* directory);
void log_security_book_depth(const SecurityBook& book, const ItchSecurityDirectory* directory, std::size_t max_levels);
void log_book_summary(const ItchBook& book);

}  // namespace ItchLog
