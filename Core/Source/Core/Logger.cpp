//
// Created by Stephen Linder on 8/31/26.
//

#include "Logger.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <vector>

#include "Book.h"
#include "Order.h"
#include "SecurityBook.h"
#include "StockDirectory.h"

namespace {

std::mutex g_mutex;
Logger::Level g_min_level = Logger::Level::Info;
std::ostream* g_output = &std::cerr;

const char* level_name(Logger::Level level)
{
    switch (level) {
    case Logger::Level::Error:
        return "ERROR";
    case Logger::Level::Warn:
        return "WARN";
    case Logger::Level::Info:
        return "INFO";
    case Logger::Level::Debug:
        return "DEBUG";
    }
    return "?";
}

const char* side_name(Side side)
{
    return side == Side::BID ? "BID" : "ASK";
}

std::string format_price(uint32_t price)
{
    std::ostringstream out;
    out << (price / 10'000u) << '.' << std::setw(4) << std::setfill('0') << (price % 10'000u);
    return out.str();
}

std::string symbol_for_locate(const StockDirectory* directory, uint16_t stock_locate)
{
    if (directory == nullptr) {
        return "locate=" + std::to_string(stock_locate);
    }
    const auto symbol = directory->lookup(stock_locate);
    if (!symbol.has_value()) {
        return "locate=" + std::to_string(stock_locate);
    }
    return *symbol + " (locate=" + std::to_string(stock_locate) + ')';
}

uint32_t level_total_shares(const Level& level)
{
    uint32_t total = 0;
    for (const Order& order : level.orders) {
        total += order.quantity();
    }
    return total;
}

void write(Logger::Level level, const std::string& message)
{
    std::lock_guard lock(g_mutex);
    if (static_cast<int>(level) > static_cast<int>(g_min_level)) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif

    (*g_output) << std::put_time(&local_tm, "%F %T") << '.' << std::setw(3) << std::setfill('0') << ms.count()
                << ' ' << level_name(level) << ' ' << message << '\n';
}

}  // namespace

namespace Logger {

void set_min_level(Level level)
{
    {
        std::lock_guard lock(g_mutex);
        g_min_level = level;
    }
    debug("min level updated");
}

Level min_level()
{
    std::lock_guard lock(g_mutex);
    return g_min_level;
}

void set_output_stream(std::ostream& stream)
{
    std::lock_guard lock(g_mutex);
    g_output = &stream;
}

void error(const std::string& message)
{
    write(Level::Error, message);
}

void warn(const std::string& message)
{
    write(Level::Warn, message);
}

void info(const std::string& message)
{
    write(Level::Info, message);
}

void debug(const std::string& message)
{
    write(Level::Debug, message);
}

void log_feed_open(const std::string& path)
{
    info("feed open path=" + path);
}

void log_feed_eof(const std::string& path, uint64_t frames_read)
{
    info("feed eof path=" + path + " frames=" + std::to_string(frames_read));
}

void log_decode_skipped(char message_type, uint16_t stock_locate)
{
    debug(std::string("decode skipped type='") + message_type + "' locate=" + std::to_string(stock_locate));
}

void log_queue_depth(std::size_t depth, std::size_t capacity)
{
    debug("queue depth=" + std::to_string(depth) + " capacity=" + std::to_string(capacity));
}

void log_directory_add(uint16_t stock_locate, const std::string& symbol, char market_category)
{
    info("directory add locate=" + std::to_string(stock_locate) + " symbol=" + symbol + " market_category=" +
         market_category);
}

void log_directory_snapshot(const StockDirectory& directory)
{
    std::ostringstream out;
    out << "directory snapshot entries=" << directory.locates().size();
    for (const uint16_t locate : directory.locates()) {
        const auto symbol = directory.lookup(locate);
        out << "\n  locate=" << locate;
        if (symbol.has_value()) {
            out << " symbol=" << *symbol;
        }
    }
    debug(out.str());
}

void log_order(const Order& order)
{
    std::ostringstream out;
    out << "order ref=" << order.order_reference_number() << " locate=" << order.stock_locate()
        << " side=" << side_name(order.side()) << " price=" << format_price(order.price())
        << " qty=" << order.quantity() << " ts_ns=" << order.timestamp_ns();
    debug(out.str());
}

void log_order_add(const Order& order)
{
    std::ostringstream out;
    out << "book add ref=" << order.order_reference_number() << " locate=" << order.stock_locate()
        << " side=" << side_name(order.side()) << " price=" << format_price(order.price())
        << " qty=" << order.quantity();
    debug(out.str());
}

void log_order_delete(uint16_t stock_locate, uint64_t order_reference_number)
{
    debug("book delete locate=" + std::to_string(stock_locate) + " ref=" + std::to_string(order_reference_number));
}

void log_order_execute(uint16_t stock_locate,
                       uint64_t order_reference_number,
                       uint32_t executed_shares,
                       uint32_t quantity_remaining)
{
    std::ostringstream out;
    out << "book execute locate=" << stock_locate << " ref=" << order_reference_number
        << " executed=" << executed_shares << " remaining=" << quantity_remaining;
    debug(out.str());
}

void log_order_cancel(uint16_t stock_locate,
                      uint64_t order_reference_number,
                      uint32_t cancelled_shares,
                      uint32_t quantity_remaining)
{
    std::ostringstream out;
    out << "book cancel locate=" << stock_locate << " ref=" << order_reference_number
        << " cancelled=" << cancelled_shares << " remaining=" << quantity_remaining;
    debug(out.str());
}

void log_order_replace(uint16_t stock_locate,
                       uint64_t old_order_reference_number,
                       uint64_t new_order_reference_number,
                       uint32_t new_price,
                       uint32_t new_shares)
{
    std::ostringstream out;
    out << "book replace locate=" << stock_locate << " old_ref=" << old_order_reference_number
        << " new_ref=" << new_order_reference_number << " price=" << format_price(new_price)
        << " qty=" << new_shares;
    debug(out.str());
}

void log_unknown_order(const char* action, uint16_t stock_locate, uint64_t order_reference_number)
{
    warn(std::string("book unknown ") + action + " locate=" + std::to_string(stock_locate) +
         " ref=" + std::to_string(order_reference_number));
}

void log_overfill(const char* action,
                  uint16_t stock_locate,
                  uint64_t order_reference_number,
                  uint32_t requested_shares,
                  uint32_t quantity_remaining)
{
    warn(std::string("book overfill ") + action + " locate=" + std::to_string(stock_locate) +
         " ref=" + std::to_string(order_reference_number) + " requested=" + std::to_string(requested_shares) +
         " remaining=" + std::to_string(quantity_remaining));
}

void log_security_book_summary(const SecurityBook& book, const StockDirectory* directory)
{
    std::ostringstream out;
    out << "security book " << symbol_for_locate(directory, book.stock_locate())
        << " bid_levels=" << book.bid_levels.size() << " ask_levels=" << book.ask_levels.size()
        << " resting_orders=" << book.order_mapping.size();
    info(out.str());
}

void log_security_book_top(const SecurityBook& book, const StockDirectory* directory)
{
    std::ostringstream out;
    out << "top of book " << symbol_for_locate(directory, book.stock_locate());

    if (!book.bid_levels.empty()) {
        const auto& level = book.bid_levels.begin()->second;
        out << " best_bid=" << format_price(book.bid_levels.begin()->first)
            << " shares=" << level_total_shares(level) << " orders=" << level.orders.size();
    } else {
        out << " best_bid=none";
    }

    if (!book.ask_levels.empty()) {
        const auto& level = book.ask_levels.begin()->second;
        out << " best_ask=" << format_price(book.ask_levels.begin()->first)
            << " shares=" << level_total_shares(level) << " orders=" << level.orders.size();
    } else {
        out << " best_ask=none";
    }

    info(out.str());
}

void log_security_book_depth(const SecurityBook& book, const StockDirectory* directory, std::size_t max_levels)
{
    std::ostringstream out;
    out << "depth " << symbol_for_locate(directory, book.stock_locate()) << " max_levels=" << max_levels;

    out << "\n  bids:";
    std::size_t bid_count = 0;
    for (const auto& [price, level] : book.bid_levels) {
        if (bid_count >= max_levels) {
            break;
        }
        out << "\n    price=" << format_price(price) << " shares=" << level_total_shares(level)
            << " orders=" << level.orders.size();
        for (const Order& order : level.orders) {
            out << "\n      ref=" << order.order_reference_number() << " qty=" << order.quantity();
        }
        ++bid_count;
    }

    out << "\n  asks:";
    std::size_t ask_count = 0;
    for (const auto& [price, level] : book.ask_levels) {
        if (ask_count >= max_levels) {
            break;
        }
        out << "\n    price=" << format_price(price) << " shares=" << level_total_shares(level)
            << " orders=" << level.orders.size();
        for (const Order& order : level.orders) {
            out << "\n      ref=" << order.order_reference_number() << " qty=" << order.quantity();
        }
        ++ask_count;
    }

    debug(out.str());
}

void log_book_summary(const Book& book)
{
    const auto directory_entries = book.directory.locates();
    const auto security_entries = book.security_locates();

    std::ostringstream out;
    out << "book summary directory_entries=" << directory_entries.size()
        << " security_books=" << security_entries.size();
    info(out.str());

    log_directory_snapshot(book.directory);

    for (const uint16_t locate : security_entries) {
        const auto security_book = book.find(locate);
        if (!security_book.has_value() || *security_book == nullptr) {
            continue;
        }
        log_security_book_summary(**security_book, &book.directory);
        log_security_book_top(**security_book, &book.directory);
    }
}

}  // namespace Logger
