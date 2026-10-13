#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <databento/record.hpp>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <span>
#include <string>

#include "Core/Book.h"
#include "Message/Databento/HandleMessage.h"

class MboSession {
public:
    MboSession(std::string symbol, std::chrono::nanoseconds print_interval)
        : symbol_(std::move(symbol)), print_interval_ns_(static_cast<std::uint64_t>(print_interval.count()))
    {
    }

    void log_depth_to(std::ostream& out, std::size_t levels)
    {
        depth_log_ = &out;
        depth_levels_ = levels;
        out << "ts_ns,side,level,price,size\n" << std::fixed << std::setprecision(2);
    }

    void on_record(const databento::Record& rec)
    {
        if (const auto* error = rec.GetIf<databento::ErrorMsg>()) {
            ++gateway_errors_;
            std::cerr << "gateway error: " << error->Err() << '\n';
            return;
        }
        if (const auto* system = rec.GetIf<databento::SystemMsg>()) {
            if (!system->IsHeartbeat()) {
                std::cout << "gateway: " << system->Msg() << '\n';
            }
            return;
        }

        const auto* mbo = rec.GetIf<databento::MboMsg>();
        if (mbo == nullptr) {
            return;
        }
        count(*mbo);

        const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(mbo), sizeof(databento::MboMsg));
        if (const auto message = mbo::MboMessage::parse(bytes)) {
            mbo::handle_message(*message, &book_);
        }

        const std::uint64_t ts_recv_ns = mbo->ts_recv.time_since_epoch().count();
        const bool due = ts_recv_ns - last_print_ns_ >= print_interval_ns_;
        if (mbo->flags.IsLast() && !mbo->flags.IsSnapshot() && due) {
            last_print_ns_ = ts_recv_ns;
            if (const auto security_book = book_.find(mbo->hd.instrument_id)) {
                if (depth_log_ != nullptr) {
                    write_depth(ts_recv_ns, **security_book);
                } else {
                    print_top_of_book(ts_recv_ns, **security_book);
                }
            }
        }
    }

    void print_summary() const
    {
        std::cout << "\n--- " << symbol_ << " summary ---\n"
                  << "snapshot records: " << snapshot_records_ << '\n'
                  << "live records:     " << live_records_ << '\n'
                  << "adds=" << adds_ << " cancels=" << cancels_ << " modifies=" << modifies_
                  << " fills=" << fills_ << " trades=" << trades_ << " clears=" << clears_ << '\n'
                  << "gateway errors:   " << gateway_errors_ << '\n'
                  << "crossed prints:   " << crossed_prints_ << '\n';

        const auto security_book = book_.find(last_instrument_id_);
        if (!security_book) {
            std::cout << "no book was built\n";
            return;
        }
        std::cout << "resting orders=" << (*security_book)->order_mapping.size()
                  << " bid levels=" << (*security_book)->bid_levels.size()
                  << " ask levels=" << (*security_book)->ask_levels.size() << '\n';
    }

    std::uint64_t records() const { return snapshot_records_ + live_records_; }

private:
    static constexpr double kPriceScale = 1e9;

    void count(const databento::MboMsg& mbo)
    {
        if (mbo.flags.IsSnapshot()) {
            ++snapshot_records_;
        } else {
            ++live_records_;
        }
        last_instrument_id_ = mbo.hd.instrument_id;
        switch (mbo.action) {
        case databento::Action::Add:
            ++adds_;
            break;
        case databento::Action::Cancel:
            ++cancels_;
            break;
        case databento::Action::Modify:
            ++modifies_;
            break;
        case databento::Action::Fill:
            ++fills_;
            break;
        case databento::Action::Trade:
            ++trades_;
            break;
        case databento::Action::Clear:
            ++clears_;
            break;
        default:
            break;
        }
    }

    void write_depth(std::uint64_t ts_recv_ns, const SecurityBook& book)
    {
        std::size_t level = 0;
        for (const auto& summary : depth_snapshot(book.bid_levels, depth_levels_)) {
            *depth_log_ << ts_recv_ns << ",bid," << level++ << ',' << summary.price / kPriceScale << ','
                        << summary.total_shares << '\n';
        }
        level = 0;
        for (const auto& summary : depth_snapshot(book.ask_levels, depth_levels_)) {
            *depth_log_ << ts_recv_ns << ",ask," << level++ << ',' << summary.price / kPriceScale << ','
                        << summary.total_shares << '\n';
        }
    }

    void print_top_of_book(std::uint64_t ts_recv_ns, const SecurityBook& book)
    {
        const std::uint64_t seconds_of_day = (ts_recv_ns / 1'000'000'000ULL) % 86'400ULL;
        char clock[16];
        std::snprintf(clock, sizeof(clock), "%02llu:%02llu:%02llu",
                      static_cast<unsigned long long>(seconds_of_day / 3600),
                      static_cast<unsigned long long>((seconds_of_day / 60) % 60),
                      static_cast<unsigned long long>(seconds_of_day % 60));

        const auto bids = depth_snapshot(book.bid_levels, 1);
        const auto asks = depth_snapshot(book.ask_levels, 1);
        std::cout << clock << " UTC  " << symbol_ << "  orders=" << book.order_mapping.size();
        if (!bids.empty()) {
            std::cout << "  bid " << bids.front().total_shares << " @ " << bids.front().price / kPriceScale;
        }
        if (!asks.empty()) {
            std::cout << "  ask " << asks.front().total_shares << " @ " << asks.front().price / kPriceScale;
        }
        if (!bids.empty() && !asks.empty() && bids.front().price >= asks.front().price) {
            ++crossed_prints_;
            std::cout << "  CROSSED";
        }
        std::cout << '\n';
    }

    std::string symbol_;
    std::uint64_t print_interval_ns_;
    std::uint64_t last_print_ns_ = 0;
    Book book_;
    std::ostream* depth_log_ = nullptr;
    std::size_t depth_levels_ = 0;

    std::uint64_t snapshot_records_ = 0;
    std::uint64_t live_records_ = 0;
    std::uint64_t adds_ = 0;
    std::uint64_t cancels_ = 0;
    std::uint64_t modifies_ = 0;
    std::uint64_t fills_ = 0;
    std::uint64_t trades_ = 0;
    std::uint64_t clears_ = 0;
    std::uint64_t gateway_errors_ = 0;
    std::uint64_t crossed_prints_ = 0;
    std::uint32_t last_instrument_id_ = 0;
};
