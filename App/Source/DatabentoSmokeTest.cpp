#include <chrono>
#include <cstdlib>
#include <databento/live.hpp>
#include <databento/live_threaded.hpp>
#include <databento/symbol_map.hpp>
#include <iostream>
#include <thread>

namespace db = databento;

int main(int argc, char* argv[])
{
    const std::string symbol = argc > 1 ? argv[1] : "ES.FUT";

    db::PitSymbolMap symbol_mappings;

    auto client = db::LiveThreaded::Builder()
                      .SetKeyFromEnv()
                      .SetDataset(db::Dataset::GlbxMdp3)
                      .BuildThreaded();

    auto handler = [&symbol_mappings](const db::Record& rec) {
        symbol_mappings.OnRecord(rec);
        if (const auto* mbo = rec.GetIf<db::MboMsg>()) {
            std::cout << symbol_mappings[mbo->hd.instrument_id] << ' ' << *mbo << '\n';
        }
        return db::KeepGoing::Continue;
    };

    client.Subscribe({symbol}, db::Schema::Mbo, db::SType::Parent);
    client.Start(handler);
    std::this_thread::sleep_for(std::chrono::seconds{15});
    return 0;
}
