#include <chrono>
#include <cstdlib>
#include <databento/dbn_store.hpp>
#include <databento/exceptions.hpp>
#include <exception>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <memory>
#include <span>
#include <vector>
#include <string>
#include <thread>
#include <unordered_map>

#include "MboSession.h"

namespace db = databento;

namespace {

class MarketClock {
public:
    explicit MarketClock(double speed) : speed_(speed) {}

    void wait_for(const db::MboMsg& mbo)
    {
        if (speed_ <= 0.0 || mbo.flags.IsSnapshot()) {
            return;
        }
        const std::uint64_t market_ns = mbo.ts_recv.time_since_epoch().count();
        if (!started_) {
            started_ = true;
            first_market_ns_ = market_ns;
            wall_start_ = std::chrono::steady_clock::now();
            return;
        }
        if (market_ns <= first_market_ns_) {
            return;
        }
        const double wall_ns = static_cast<double>(market_ns - first_market_ns_) / speed_;
        std::this_thread::sleep_until(wall_start_ + std::chrono::nanoseconds(static_cast<std::int64_t>(wall_ns)));
    }

private:
    double speed_;
    bool started_ = false;
    std::uint64_t first_market_ns_ = 0;
    std::chrono::steady_clock::time_point wall_start_;
};

std::unordered_map<InstrumentId, std::string> instrument_names(const db::Metadata& metadata)
{
    std::unordered_map<InstrumentId, std::string> names;
    for (const auto& mapping : metadata.mappings) {
        for (const auto& interval : mapping.intervals) {
            if (!interval.symbol.empty()) {
                names[static_cast<InstrumentId>(std::stoul(interval.symbol))] = mapping.raw_symbol;
            }
        }
    }
    return names;
}

struct ReplayOptions {
    std::filesystem::path path;
    std::string label;
    int print_every_seconds;
    std::filesystem::path depth_path;
    std::size_t depth_levels;
    double speed;
    std::string mode;
    Threading threading;
    std::size_t shards;
    std::string book;
    std::size_t batch;
};

template <typename BookType>
int time_book_only(const ReplayOptions& options)
{
    std::vector<mbo::MboMessage> messages;
    db::DbnStore store{options.path};
    store.Replay([&](const db::Record& rec) {
        if (const auto* mbo = rec.GetIf<db::MboMsg>()) {
            const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(mbo), sizeof(db::MboMsg));
            if (auto message = mbo::MboMessage::parse(bytes)) {
                messages.push_back(*message);
            }
        }
        return db::KeepGoing::Continue;
    });

    constexpr int kPasses = 15;
    std::vector<double> ns_per_record;
    std::size_t resting_orders = 0;
    for (int pass = 0; pass < kPasses; ++pass) {
        auto book = std::make_unique<BookType>();
        const auto start = std::chrono::steady_clock::now();
        for (const mbo::MboMessage& message : messages) {
            mbo::handle_message(message, book.get());
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
        ns_per_record.push_back(ns / static_cast<double>(messages.size()));
        resting_orders = 0;
        for (const InstrumentId id : book->security_locates()) {
            resting_orders += (*book->find(id))->resting_orders();
        }
    }
    std::sort(ns_per_record.begin(), ns_per_record.end());
    std::cout << "book only: " << messages.size() << " records, " << options.book << " book, resting orders "
              << resting_orders << ", median " << ns_per_record[kPasses / 2] << " ns/record, min "
              << ns_per_record.front() << " (" << 1e3 / ns_per_record[kPasses / 2] << "M msgs/s)\n";
    return 0;
}

template <typename BookType, std::size_t MaxBatch>
int replay(const ReplayOptions& options)
{
    if (options.mode == "book") {
        return time_book_only<BookType>(options);
    }
    std::ofstream depth_file;
    MboSession<BookType, MaxBatch> session(options.label, std::chrono::seconds{options.print_every_seconds},
                                           options.threading, options.shards);
    if (!options.depth_path.empty()) {
        depth_file.open(options.depth_path);
        if (!depth_file) {
            std::cerr << "databento_replay: cannot write " << options.depth_path << '\n';
            return 1;
        }
        session.log_depth_to(depth_file, options.depth_levels);
    }
    db::DbnStore store{options.path};
    MarketClock clock(options.speed);

    const auto start = std::chrono::steady_clock::now();
    store.Replay([&](db::Metadata&& metadata) { session.name_instruments(instrument_names(metadata)); },
                 [&](const db::Record& rec) {
                     if (const auto* mbo = rec.GetIf<db::MboMsg>()) {
                         clock.wait_for(*mbo);
                     }
                     session.on_record(rec);
                     return db::KeepGoing::Continue;
                 });
    session.finish();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    session.print_summary();
    std::cout << "replayed " << session.records() << " records in " << seconds << "s";
    if (options.speed > 0.0) {
        std::cout << " (paced at " << options.speed << "x market speed)\n";
    } else {
        std::cout << " (" << static_cast<std::uint64_t>(static_cast<double>(session.records()) / seconds)
                  << " msgs/s, " << options.mode << ", " << options.book << " book, batch " << options.batch << ")\n";
    }
    return session.records() == 0 ? 1 : 0;
}

template <typename BookType>
int replay_with_batch(const ReplayOptions& options)
{
    if (options.batch == 64) {
        return replay<BookType, 64>(options);
    }
    return replay<BookType, 1>(options);
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "usage: databento_replay <file.dbn.zst> [label] [print_every_seconds] [depth.csv] [levels] [speed]"
                     " [single|ring|parse|book|shardedN] [tree|flat|hybrid|agg|agg-flat|agg-flat-cached|ladder|ladder-cached|ladder-cached64|ladder-identity] [1|64]\n"
                  << "  print_every_seconds 0 turns off printing and counting, for benchmarks\n"
                  << "  speed 0 replays as fast as possible, 1 at market pace, 60 at one market minute per second\n";
        return 1;
    }

    ReplayOptions options;
    options.path = argv[1];
    options.label = argc > 2 ? argv[2] : options.path.stem().string();
    options.print_every_seconds = argc > 3 ? std::atoi(argv[3]) : 60;
    options.depth_path = argc > 4 ? argv[4] : "";
    options.depth_levels = argc > 5 ? static_cast<std::size_t>(std::atoi(argv[5])) : 40;
    options.speed = argc > 6 ? std::atof(argv[6]) : 0.0;
    options.mode = argc > 7 ? argv[7] : "single";
    const bool sharded = options.mode.starts_with("sharded");
    options.shards = sharded ? std::stoul(options.mode.substr(7)) : 1;
    options.threading = options.mode == "ring"    ? Threading::Pipeline
                        : options.mode == "parse" ? Threading::ParseOnly
                        : sharded                 ? Threading::Sharded
                                                  : Threading::Single;
    options.book = argc > 8 ? argv[8] : "tree";
    options.batch = argc > 9 ? static_cast<std::size_t>(std::atoi(argv[9])) : 1;

    if (!std::filesystem::exists(options.path)) {
        std::cerr << "databento_replay: no such file " << options.path << " (run make mbo_download first)\n";
        return 1;
    }

    try {
        if (options.book == "flat") {
            return replay_with_batch<FlatBook>(options);
        }
        if (options.book == "hybrid") {
            return replay_with_batch<HybridFlatBook>(options);
        }
        if (options.book == "agg") {
            return replay_with_batch<AggregateBook>(options);
        }
        if (options.book == "agg-flat") {
            return replay_with_batch<AggregateFlatIndexBook>(options);
        }
        if (options.book == "agg-flat-cached") {
            return replay_with_batch<CachedAggregateFlatIndexBook>(options);
        }
        if (options.book == "ladder") {
            return replay_with_batch<LadderBook>(options);
        }
        if (options.book == "ladder-cached") {
            return replay_with_batch<CachedLadderBook>(options);
        }
        if (options.book == "ladder-cached64") {
            return replay_with_batch<DirectCachedLadderBook>(options);
        }
        if (options.book == "ladder-identity") {
            return replay_with_batch<IdentityLadderBook>(options);
        }
        if (options.book != "tree") {
            std::cerr << "databento_replay: unknown book " << options.book << '\n';
            return 1;
        }
        return replay_with_batch<Book>(options);
    } catch (const db::Exception& e) {
        std::cerr << "databento error: " << e.what() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
    return 1;
}
