#include <chrono>
#include <cstdlib>
#include <databento/live.hpp>
#include <databento/live_threaded.hpp>
#include <databento/symbol_map.hpp>
#include <iostream>
#include <thread>
#include <vector>

#include "Core/Book.h"
#include "Databento/DecodeMessage.h"
#include "Databento/HandleMessage.h"

namespace db = databento;

namespace {

constexpr double kPriceScale = 1e9;

void print_top_of_book(const std::string& symbol, const SecurityBook& book)
{
    const auto bids = depth_snapshot(book.bid_levels, 1);
    const auto asks = depth_snapshot(book.ask_levels, 1);
    std::cout << symbol << "  orders=" << book.order_mapping.size();
    if (!bids.empty()) {
        std::cout << "  bid " << bids.front().total_shares << " @ " << bids.front().price / kPriceScale;
    }
    if (!asks.empty()) {
        std::cout << "  ask " << asks.front().total_shares << " @ " << asks.front().price / kPriceScale;
    }
    std::cout << '\n';
}

}  // namespace

int main(int argc, char* argv[])
{
    const std::string symbol = argc > 1 ? argv[1] : "ESZ6";
    const int seconds = argc > 2 ? std::atoi(argv[2]) : 15;

    db::PitSymbolMap symbol_mappings;
    Book book;
    auto last_print = std::chrono::steady_clock::now();

    auto client = db::LiveThreaded::Builder()
                      .SetKeyFromEnv()
                      .SetDataset(db::Dataset::GlbxMdp3)
                      .BuildThreaded();

    auto handler = [&](const db::Record& rec) {
        symbol_mappings.OnRecord(rec);
        const auto* mbo = rec.GetIf<db::MboMsg>();
        if (mbo == nullptr) {
            return db::KeepGoing::Continue;
        }

        const auto* bytes = reinterpret_cast<const uint8_t*>(mbo);
        const std::vector<uint8_t> data(bytes, bytes + sizeof(db::MboMsg));
        mbo::handle_message(mbo::decode_message(data), &book);

        const bool event_complete = mbo->flags.IsLast();
        const bool in_snapshot = mbo->flags.IsSnapshot();
        const auto now = std::chrono::steady_clock::now();
        if (event_complete && !in_snapshot && now - last_print >= std::chrono::milliseconds{250}) {
            last_print = now;
            if (const auto security_book = book.find(mbo->hd.instrument_id)) {
                print_top_of_book(symbol_mappings[mbo->hd.instrument_id], **security_book);
            }
        }
        return db::KeepGoing::Continue;
    };

    client.SubscribeWithSnapshot({symbol}, db::Schema::Mbo, db::SType::RawSymbol);
    client.Start(handler);
    std::this_thread::sleep_for(std::chrono::seconds{seconds});
    return 0;
}
