#pragma once

#include <chrono>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <databento/record.hpp>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Book.h"
#include "Core/MessagePipeline.h"
#include "Message/Databento/HandleMessage.h"

enum class Threading {
    Single,
    Pipeline,
    ParseOnly,
    Sharded,
};

class MboSession {
public:
    MboSession(std::string symbol, std::chrono::nanoseconds print_interval, Threading threading = Threading::Single,
               std::size_t shards = 1)
        : symbol_(std::move(symbol)),
          print_interval_ns_(static_cast<std::uint64_t>(print_interval.count())),
          parse_only_(threading == Threading::ParseOnly)
    {
        if (threading == Threading::Pipeline) {
            pipeline_.emplace(ApplyOnConsumer{this});
        }
        if (threading == Threading::Sharded) {
            for (std::size_t i = 0; i < shards; ++i) {
                shards_.push_back(std::make_unique<Shard>());
            }
        }
    }

    void name_instruments(std::unordered_map<InstrumentId, std::string> names)
    {
        names_ = std::move(names);
        const std::string depth_symbol = symbol_.substr(0, symbol_.find(','));
        for (const auto& [id, name] : names_) {
            if (name == depth_symbol) {
                depth_instrument_ = id;
            }
        }
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

        const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(mbo), sizeof(databento::MboMsg));
        const auto message = mbo::MboMessage::parse(bytes);
        if (!message) {
            ++skipped_records_;
            if (mbo->flags.IsLast()) {
                ++skipped_last_records_;
            }
            return;
        }
        if (parse_only_) {
            ++live_records_;
            return;
        }
        if (!shards_.empty()) {
            count(*message);
            Shard& shard = *shards_[message->instrument_id() % shards_.size()];
            ++shard.records;
            shard.pipeline.push(*message);
            return;
        }
        if (pipeline_) {
            pipeline_->push(*message);
        } else {
            apply(*message);
        }
    }

    void finish()
    {
        if (pipeline_) {
            pipeline_->finish();
        }
        for (auto& shard : shards_) {
            shard->pipeline.finish();
            book_.absorb(std::move(shard->book));
        }
    }

    void print_summary() const
    {
        std::cout << "\n--- " << symbol_ << " summary ---\n"
                  << "snapshot records: " << snapshot_records_ << '\n'
                  << "live records:     " << live_records_ << '\n'
                  << "adds=" << adds_ << " cancels=" << cancels_ << " modifies=" << modifies_
                  << " fills=" << fills_ << " trades=" << trades_ << " clears=" << clears_ << '\n'
                  << "skipped records:  " << skipped_records_ << " (" << skipped_last_records_ << " with F_LAST)\n"
                  << "gateway errors:   " << gateway_errors_ << '\n'
                  << "crossed prints:   " << crossed_prints_ << '\n';

        std::vector<std::pair<std::string, InstrumentId>> instruments;
        for (const InstrumentId id : book_.security_locates()) {
            instruments.emplace_back(name_of(id), id);
        }
        for (std::size_t i = 0; i < shards_.size(); ++i) {
            std::cout << "shard " << i << " records=" << shards_[i]->records << '\n';
        }
        if (instruments.empty()) {
            std::cout << "no book was built\n";
            return;
        }
        std::sort(instruments.begin(), instruments.end());
        for (const auto& [name, id] : instruments) {
            const SecurityBook& book = **book_.find(id);
            std::cout << std::left << std::setw(12) << name << std::right << " resting orders="
                      << book.order_mapping.size() << " bid levels=" << book.bid_levels.size()
                      << " ask levels=" << book.ask_levels.size() << '\n';
        }
    }

    std::uint64_t records() const { return snapshot_records_ + live_records_; }

private:
    static constexpr double kPriceScale = 1e9;

    static constexpr std::size_t kQueueCapacity = 1 << 16;

    struct ApplyOnConsumer {
        MboSession* session;
        void operator()(const mbo::MboMessage& message) const { session->apply(message); }
    };

    struct ApplyToBook {
        Book* book;
        void operator()(const mbo::MboMessage& message) const { mbo::handle_message(message, book); }
    };

    struct Shard {
        Book book;
        std::uint64_t records = 0;
        MessagePipeline<mbo::MboMessage, kQueueCapacity, ApplyToBook, BackoffWhenIdle> pipeline{ApplyToBook{&book}};
    };

    void apply(const mbo::MboMessage& message)
    {
        count(message);
        mbo::handle_message(message, &book_);

        if (!message.is_last() || message.is_snapshot()) {
            return;
        }
        const InstrumentId id = message.instrument_id();
        if (depth_log_ != nullptr && depth_instrument_ && *depth_instrument_ != id) {
            return;
        }
        const std::uint64_t ts_recv_ns = message.ts_recv();
        std::uint64_t& last_print_ns = last_print_ns_[id];
        if (ts_recv_ns - last_print_ns < print_interval_ns_) {
            return;
        }
        last_print_ns = ts_recv_ns;
        if (const auto security_book = book_.find(id)) {
            if (depth_log_ != nullptr) {
                write_depth(ts_recv_ns, **security_book);
            } else {
                print_top_of_book(ts_recv_ns, name_of(id), **security_book);
            }
        }
    }

    std::string name_of(InstrumentId id) const
    {
        if (const auto it = names_.find(id); it != names_.end()) {
            return it->second;
        }
        return names_.empty() ? symbol_ : "id " + std::to_string(id);
    }

    void count(const mbo::MboMessage& message)
    {
        if (message.is_snapshot()) {
            ++snapshot_records_;
        } else {
            ++live_records_;
        }
        switch (message.action()) {
        case mbo::Action::Add:
            ++adds_;
            break;
        case mbo::Action::Cancel:
            ++cancels_;
            break;
        case mbo::Action::Modify:
            ++modifies_;
            break;
        case mbo::Action::Fill:
            ++fills_;
            break;
        case mbo::Action::Trade:
            ++trades_;
            break;
        case mbo::Action::Clear:
            ++clears_;
            break;
        case mbo::Action::None:
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

    void print_top_of_book(std::uint64_t ts_recv_ns, const std::string& name, const SecurityBook& book)
    {
        const std::uint64_t seconds_of_day = (ts_recv_ns / 1'000'000'000ULL) % 86'400ULL;
        char clock[16];
        std::snprintf(clock, sizeof(clock), "%02llu:%02llu:%02llu",
                      static_cast<unsigned long long>(seconds_of_day / 3600),
                      static_cast<unsigned long long>((seconds_of_day / 60) % 60),
                      static_cast<unsigned long long>(seconds_of_day % 60));

        const auto bids = depth_snapshot(book.bid_levels, 1);
        const auto asks = depth_snapshot(book.ask_levels, 1);
        std::cout << clock << " UTC  " << name << "  orders=" << book.order_mapping.size();
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
    bool parse_only_;
    std::unordered_map<InstrumentId, std::uint64_t> last_print_ns_;
    std::unordered_map<InstrumentId, std::string> names_;
    std::optional<InstrumentId> depth_instrument_;
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
    std::uint64_t skipped_records_ = 0;
    std::uint64_t skipped_last_records_ = 0;

    std::optional<MessagePipeline<mbo::MboMessage, kQueueCapacity, ApplyOnConsumer, BackoffWhenIdle>> pipeline_;
    std::vector<std::unique_ptr<Shard>> shards_;
};
