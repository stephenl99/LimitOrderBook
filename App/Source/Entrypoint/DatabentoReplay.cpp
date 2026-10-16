#include <chrono>
#include <cmath>
#include <cstdlib>
#include <databento/dbn_store.hpp>
#include <databento/exceptions.hpp>
#include <exception>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <memory>
#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <utility>
#include <thread>
#include <unordered_map>

#include "BookVariants/BookVariants.h"
#include "BookVariants/OrderFlow.h"
#include "Session/DatabentoSession.h"

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

std::unordered_map<InstrumentId, Price> instrument_ticks(const std::filesystem::path& path)
{
    std::unordered_map<InstrumentId, Price> first_prices;
    std::unordered_map<InstrumentId, Price> ticks;
    db::DbnStore store{path};
    store.Replay([&](const db::Record& rec) {
        const auto* mbo = rec.GetIf<db::MboMsg>();
        if (mbo == nullptr || (mbo->action != db::action::Add && mbo->action != db::action::Modify)) {
            return db::KeepGoing::Continue;
        }
        const InstrumentId id = mbo->hd.instrument_id;
        const Price first = first_prices.try_emplace(id, mbo->price).first->second;
        Price& tick = ticks[id];
        tick = std::gcd(tick, mbo->price - first);
        return db::KeepGoing::Continue;
    });
    for (auto& [id, tick] : ticks) {
        tick = std::max<Price>(tick, 1);
    }
    return ticks;
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
    std::string handoff;
    std::filesystem::path latency_path;
};

std::vector<mbo::MboMessage> load_messages(const std::filesystem::path& path)
{
    std::vector<mbo::MboMessage> messages;
    db::DbnStore store{path};
    store.Replay([&](const db::Record& rec) {
        if (const auto* mbo = rec.GetIf<db::MboMsg>()) {
            const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(mbo), sizeof(db::MboMsg));
            if (auto message = mbo::MboMessage::parse(bytes)) {
                messages.push_back(*message);
            }
        }
        return db::KeepGoing::Continue;
    });
    return messages;
}

struct BookTiming {
    double median_ns;
    double min_ns;
    std::size_t resting_orders;
    std::uint64_t depth_hash;
};

template <typename BookType>
std::uint64_t depth_hash(const BookType& book)
{
    std::uint64_t hash = 0xcbf29ce484222325ull;
    const auto mix = [&](std::uint64_t value) {
        hash ^= value;
        hash *= 0x100000001b3ull;
    };
    std::vector<InstrumentId> ids = book.security_locates();
    std::sort(ids.begin(), ids.end());
    for (const InstrumentId id : ids) {
        const auto& security_book = **book.find(id);
        mix(id);
        for (const LevelSummary& level : security_book.bid_depth(SIZE_MAX)) {
            mix(static_cast<std::uint64_t>(level.price));
            mix(level.total_shares);
        }
        mix(~std::uint64_t{0});
        for (const LevelSummary& level : security_book.ask_depth(SIZE_MAX)) {
            mix(static_cast<std::uint64_t>(level.price));
            mix(level.total_shares);
        }
    }
    return hash;
}

template <typename BookType>
BookTiming time_book(const std::vector<mbo::MboMessage>& messages, const std::unordered_map<InstrumentId, Price>& ticks)
{
    constexpr int kPasses = 15;
    std::vector<double> ns_per_record;
    std::size_t resting_orders = 0;
    std::uint64_t hash = 0;
    for (int pass = 0; pass < kPasses; ++pass) {
        auto book = std::make_unique<BookType>();
        for (const auto& [id, tick] : ticks) {
            book->set_tick(id, tick);
        }
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
        if (pass + 1 == kPasses) {
            hash = depth_hash(*book);
        }
    }
    std::sort(ns_per_record.begin(), ns_per_record.end());
    return BookTiming{ns_per_record[kPasses / 2], ns_per_record.front(), resting_orders, hash};
}

//this is just for timing the book and doing no file reading in the critical path. so you just parse the messages first and then run them instead of doing that as you go
template <typename BookType>
int time_book_only(const ReplayOptions& options)
{
    const auto messages = load_messages(options.path);
    const BookTiming timing = time_book<BookType>(messages, instrument_ticks(options.path));
    std::cout << "book only: " << messages.size() << " records, " << options.book << " book, resting orders "
              << timing.resting_orders << ", median " << timing.median_ns << " ns/record, min " << timing.min_ns
              << " (" << 1e3 / timing.median_ns << "M msgs/s)\n";
    return 0;
}

bool selected(std::string_view filter, std::string_view name)
{
    if (filter == "all") {
        return true;
    }
    while (!filter.empty()) {
        const std::size_t comma = filter.find(',');
        if (filter.substr(0, comma) == name) {
            return true;
        }
        filter = comma == std::string_view::npos ? std::string_view{} : filter.substr(comma + 1);
    }
    return false;
}

int time_variants(const ReplayOptions& options)
{
    const auto messages = load_messages(options.path);
    const auto ticks = instrument_ticks(options.path);
    std::cout << "book,levels,index,orders,cache,median_ns,min_ns,resting_orders,depth_hash\n";
    bool any = false;
    for_each_book_variant([&]<typename BookType>(std::type_identity<BookType>, const BookVariantInfo& info) {
        if (!selected(options.book, info.name)) {
            return;
        }
        any = true;
        const BookTiming timing = time_book<BookType>(messages, ticks);
        std::cout << info.name << ',' << info.levels << ',' << info.index << ',' << info.orders << ','
                  << info.cache_slots << ',' << timing.median_ns << ',' << timing.min_ns << ','
                  << timing.resting_orders << ',' << std::hex << timing.depth_hash << std::dec << std::endl;
    });
    if (!any) {
        std::cerr << "databento_replay: no book variant matches " << options.book << '\n';
        return 1;
    }
    return 0;
}

int export_flow(const ReplayOptions& options)
{
    const auto messages = load_messages(options.path);
    const auto ticks = instrument_ticks(options.path);
    OrderFlow flow;
    std::unordered_map<InstrumentId, std::uint32_t> dense;
    struct Live {
        std::uint32_t instrument;
        std::uint32_t quantity;
    };
    std::unordered_map<std::uint64_t, Live> live;
    const auto instrument_for = [&](InstrumentId id) {
        const auto [it, inserted] = dense.try_emplace(id, static_cast<std::uint32_t>(dense.size()));
        if (inserted) {
            const auto tick = ticks.find(id);
            flow.ticks.push_back(tick == ticks.end() ? 1 : tick->second);
        }
        return it->second;
    };
    for (const mbo::MboMessage& message : messages) {
        const ::Side side = message.side() == mbo::Side::Bid ? ::Side::BID : ::Side::ASK;
        const std::uint64_t id = message.order_id();
        switch (message.action()) {
        case mbo::Action::Add: {
            const std::uint32_t instrument = instrument_for(message.instrument_id());
            flow.ops.push_back(BookOp{BookOpType::Add, side, instrument, message.size(), id, 0, message.raw_price()});
            live[id] = Live{instrument, message.size()};
            break;
        }
        case mbo::Action::Cancel: {
            const std::uint32_t instrument = instrument_for(message.instrument_id());
            flow.ops.push_back(BookOp{BookOpType::Cancel, side, instrument, message.size(), id, 0, 0});
            const auto it = live.find(id);
            if (it != live.end() && it->second.quantity <= message.size()) {
                live.erase(it);
            } else if (it != live.end()) {
                it->second.quantity -= message.size();
            }
            break;
        }
        case mbo::Action::Modify: {
            const std::uint32_t instrument = instrument_for(message.instrument_id());
            flow.ops.push_back(BookOp{BookOpType::Modify, side, instrument, message.size(), id, 0, message.raw_price()});
            if (message.size() == 0) {
                live.erase(id);
            } else if (const auto it = live.find(id); it != live.end()) {
                it->second.quantity = message.size();
            }
            break;
        }
        case mbo::Action::Clear: {
            const std::uint32_t instrument = instrument_for(message.instrument_id());
            flow.ops.push_back(BookOp{BookOpType::Clear, side, instrument, 0, 0, 0, 0});
            std::erase_if(live, [&](const auto& entry) { return entry.second.instrument == instrument; });
            break;
        }
        case mbo::Action::Fill:
        case mbo::Action::Trade:
        case mbo::Action::None:
            break;
        }
        flow.peak_resting = std::max(flow.peak_resting, live.size());
    }
    save_order_flow(flow, options.depth_path);
    std::cout << "exported " << flow.ops.size() << " book operations from " << messages.size() << " records, "
              << flow.ticks.size() << " instruments, peak resting " << flow.peak_resting << " to "
              << options.depth_path << '\n';
    return 0;
}

double percentile(const std::vector<std::uint32_t>& sorted, double p)
{
    return sorted.empty() ? 0.0 : sorted[static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1))];
}

void write_latency(const ReplayOptions& options, std::vector<std::uint32_t> latencies)
{
    std::sort(latencies.begin(), latencies.end());
    const bool fresh = !std::filesystem::exists(options.latency_path);
    std::ofstream out(options.latency_path, std::ios::app);
    if (fresh) {
        out << "file,mode,book,handoff,batch,speed,count,p50_ns,p90_ns,p99_ns,p999_ns,p9999_ns,max_ns\n";
    }
    out << options.path.filename().string() << ',' << options.mode << ',' << options.book << ',' << options.handoff
        << ',' << options.batch << ',' << options.speed << ',' << latencies.size() << ',' << percentile(latencies, 0.5)
        << ',' << percentile(latencies, 0.9) << ',' << percentile(latencies, 0.99) << ','
        << percentile(latencies, 0.999) << ',' << percentile(latencies, 0.9999) << ','
        << (latencies.empty() ? 0 : latencies.back()) << '\n';
    const std::filesystem::path cdf_path = options.latency_path.string() + ".cdf";
    const bool fresh_cdf = !std::filesystem::exists(cdf_path);
    std::ofstream cdf(cdf_path, std::ios::app);
    if (fresh_cdf) {
        cdf << "file,mode,book,handoff,batch,speed,p,latency_ns\n";
    }
    for (int step = 0; step <= 1000; ++step) {
        const double p = 1.0 - std::pow(10.0, -4.0 * step / 1000.0);
        cdf << options.path.filename().string() << ',' << options.mode << ',' << options.book << ',' << options.handoff
            << ',' << options.batch << ',' << options.speed << ',' << p << ',' << percentile(latencies, p) << '\n';
    }
}

template <typename BookType, typename HandoffType = DefaultHandoff>
int replay(const ReplayOptions& options)
{
    if (options.mode == "book") {
        return time_book_only<BookType>(options);
    }
    std::ofstream depth_file;
    DatabentoSession<BookType, HandoffType> session(options.label, std::chrono::seconds{options.print_every_seconds}, options.threading,
                                 options.shards, options.batch);
    if (!options.depth_path.empty()) {
        depth_file.open(options.depth_path);
        if (!depth_file) {
            std::cerr << "databento_replay: cannot write " << options.depth_path << '\n';
            return 1;
        }
        session.log_depth_to(depth_file, options.depth_levels);
    }
    session.set_ticks(instrument_ticks(options.path));
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
    if constexpr (HandoffType::kStamped) {
        write_latency(options, session.handoff_latencies_ns());
    }
    std::cout << "replayed " << session.records() << " records in " << seconds << "s";
    if (options.speed > 0.0) {
        std::cout << " (paced at " << options.speed << "x market speed)\n";
    } else {
        std::cout << " (" << static_cast<std::uint64_t>(static_cast<double>(session.records()) / seconds)
                  << " msgs/s, " << options.mode << ", " << options.book << " book, batch " << options.batch << ")\n";
    }
    return session.records() == 0 ? 1 : 0;
}

template <typename BookType, bool Stamped>
int replay_with_handoff(const ReplayOptions& options)
{
    if (options.handoff == "ring") {
        return replay<BookType, Handoff<CircularBuffer, BackoffWhenIdle, Stamped>>(options);
    }
    if (options.handoff == "ring-spin") {
        return replay<BookType, Handoff<CircularBuffer, SpinWhenIdle, Stamped>>(options);
    }
    if (options.handoff == "ring-yield") {
        return replay<BookType, Handoff<CircularBuffer, YieldWhenIdle, Stamped>>(options);
    }
    if (options.handoff == "sequenced") {
        return replay<BookType, Handoff<SequencedBuffer, BackoffWhenIdle, Stamped>>(options);
    }
    if (options.handoff == "locked-ring") {
        return replay<BookType, Handoff<MutexBuffer, BackoffWhenIdle, Stamped>>(options);
    }
    if (options.handoff == "mutex-cv") {
        return replay<BookType, CondvarHandoff<Stamped>>(options);
    }
    std::cerr << "databento_replay: unknown handoff " << options.handoff
              << " (ring|ring-spin|ring-yield|sequenced|locked-ring|mutex-cv)\n";
    return 1;
}

template <typename BookType>
int run_book(const ReplayOptions& options)
{
    return options.latency_path.empty() ? replay_with_handoff<BookType, false>(options)
                                        : replay_with_handoff<BookType, true>(options);
}

using ReplayFunction = int (*)(const ReplayOptions&);

using BookEntry = std::pair<std::string_view, ReplayFunction>;

constexpr BookEntry kBooks[] = {
    {"tree", run_book<Book>},
    {"flat", run_book<FlatBook>},
    {"hybrid", run_book<HybridFlatBook>},
    {"agg", run_book<AggregateBook>},
    {"agg-flat", run_book<AggregateFlatIndexBook>},
    {"ladder", run_book<LadderBook>},
    {"ladder-cached64", run_book<DirectCachedLadderBook>},
    {"ladder-identity", run_book<IdentityLadderBook>},
    {"tree-cached64", run_book<OuterBook<SecurityBook, 64>>},
    {"hybrid-cached64", run_book<OuterBook<HybridFlatSecurityBook, 64>>},
    {"agg-flat-cached64", run_book<OuterBook<SortedAggregateFlatIndexSecurityBook, 64>>},
    {"pooled-hybrid", run_book<OuterBook<PooledHybridSecurityBook, 64>>},
    {"pooled-ladder", run_book<OuterBook<PooledLadderSecurityBook, 64>>},
    {"lazy-handle-ladder", run_book<OuterBook<LazyHandleLadderSecurityBook, 64>>},
};

std::string book_names()
{
    std::string names;
    for (const auto& [name, function] : kBooks) {
        names += names.empty() ? "" : "|";
        names += name;
    }
    return names;
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "usage: databento_replay <file.dbn.zst> [label] [print_every_seconds] [depth.csv] [levels] [speed]"
                     " [single|mutex|ring|parse|book|variants|export|shardedN] ["
                  << book_names() << "] [batch] [handoff] [latency.csv]\n"
                  << "  export writes the book operations as a book_bench flow to the depth.csv argument\n"
                  << "  mutex is the ring mode with a std::queue + mutex + condition variable (the mutex-cv handoff)\n"
                  << "  handoff is ring (default), ring-spin, ring-yield, sequenced, locked-ring or mutex-cv\n"
                  << "  latency.csv stamps each message on push and appends push-to-apply percentiles\n"
                  << "  variants times every book_bench variant on the file, book only; the book argument is all"
                     " or a comma list\n"
                  << "  print_every_seconds 0 turns off printing and counting, for benchmarks\n"
                  << "  batch is the most messages the consumer drains per wake, 1 to " << DatabentoSession<>::kQueueCapacity << "\n"
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
    options.threading = options.mode == "ring" || options.mode == "mutex" ? Threading::Pipeline
                        : options.mode == "parse" ? Threading::ParseOnly
                        : sharded                 ? Threading::Sharded
                                                  : Threading::Single;
    options.book = argc > 8 ? argv[8] : (options.mode == "variants" ? "all" : "tree");
    options.batch = argc > 9 ? static_cast<std::size_t>(std::atoi(argv[9])) : 1;
    options.handoff = argc > 10 ? argv[10] : (options.mode == "mutex" ? "mutex-cv" : "ring");
    options.latency_path = argc > 11 ? argv[11] : "";

    if (options.batch < 1 || options.batch > DatabentoSession<>::kQueueCapacity) {
        std::cerr << "databento_replay: batch must be 1 to " << DatabentoSession<>::kQueueCapacity << '\n';
        return 1;
    }

    if (!std::filesystem::exists(options.path)) {
        std::cerr << "databento_replay: no such file " << options.path << " (run make mbo_download first)\n";
        return 1;
    }

    if (options.mode == "export") {
        try {
            return export_flow(options);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << '\n';
            return 1;
        }
    }

    if (options.mode == "variants") {
        try {
            return time_variants(options);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << '\n';
            return 1;
        }
    }

    const auto book = std::ranges::find(kBooks, options.book, &BookEntry::first);
    if (book == std::end(kBooks)) {
        std::cerr << "databento_replay: unknown book " << options.book << " (" << book_names() << ")\n";
        return 1;
    }

    try {
        return book->second(options);
    } catch (const db::Exception& e) {
        std::cerr << "databento error: " << e.what() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
    return 1;
}
