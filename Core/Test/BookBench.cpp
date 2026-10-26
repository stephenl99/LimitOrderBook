#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__x86_64__)
#include <x86intrin.h>
#endif

#include <unistd.h>

#include "BookVariants/BookVariants.h"
#include "Core/PerfControl.h"
#include "BookVariants/OrderFlow.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::string mode;
    std::string scenario = "synthetic";
    std::vector<std::string> impls;
    int runs = 3;
    FlowConfig flow;
    std::string itch_path;
    std::string flow_path;
    std::string out_path;
    bool header = true;
};

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find(separator, start), text.size());
        if (end > start) {
            parts.emplace_back(text.substr(start, end - start));
        }
        start = end + 1;
    }
    return parts;
}

std::optional<Options> parse(int argc, char* argv[])
{
    if (argc < 2) {
        return std::nullopt;
    }
    Options options;
    options.mode = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument(std::string(arg) + " needs a value");
            }
            return argv[++i];
        };
        if (arg == "--impl") {
            const std::string list = value();
            options.impls = list == "all" ? book_variant_names() : split(list, ',');
        } else if (arg == "--runs") {
            options.runs = std::stoi(value());
        } else if (arg == "--scenario") {
            options.scenario = value();
        } else if (arg == "--ops") {
            options.flow.ops = std::stoull(value());
        } else if (arg == "--instruments") {
            options.flow.instruments = static_cast<std::uint32_t>(std::stoul(value()));
        } else if (arg == "--resting") {
            options.flow.resting_per_instrument = std::stoull(value());
        } else if (arg == "--near-touch") {
            options.flow.near_touch = std::stod(value());
        } else if (arg == "--recent-cancel") {
            options.flow.recent_cancel = std::stod(value());
        } else if (arg == "--move") {
            options.flow.move_probability = std::stod(value());
        } else if (arg == "--far-fraction") {
            options.flow.far_fraction = std::stod(value());
        } else if (arg == "--far-ticks") {
            options.flow.far_ticks = static_cast<std::uint32_t>(std::stoul(value()));
        } else if (arg == "--seed") {
            options.flow.seed = std::stoull(value());
        } else if (arg == "--random-ids") {
            options.flow.random_ids = true;
        } else if (arg == "--itch") {
            options.itch_path = value();
        } else if (arg == "--flow") {
            options.flow_path = value();
        } else if (arg == "--out") {
            options.out_path = value();
        } else if (arg == "--no-header") {
            options.header = false;
        } else {
            throw std::invalid_argument("unknown option " + std::string(arg));
        }
    }
    if (options.impls.empty()) {
        options.impls = book_variant_names();
    }
    return options;
}

OrderFlow load_flow(const Options& options)
{
    if (!options.flow_path.empty()) {
        return read_order_flow(options.flow_path);
    }
    if (!options.itch_path.empty()) {
        auto flow = load_itch_order_flow(options.itch_path);
        if (!flow) {
            throw std::runtime_error(flow.error());
        }
        return std::move(*flow);
    }
    return generate_order_flow(options.flow);
}

template <BenchSecurityBook S>
inline void apply(S& book, const BookOp& op)
{
    switch (op.type) {
    case BookOpType::Add:
        book.add_order(op.ref, op.side, op.price, op.quantity);
        break;
    case BookOpType::Execute:
        book.execute_order(op.ref, op.quantity);
        break;
    case BookOpType::Cancel:
        book.cancel_order(op.ref, op.quantity);
        break;
    case BookOpType::Delete:
        book.delete_order(op.ref);
        break;
    case BookOpType::Replace:
        book.replace_order(op.ref, op.new_ref, op.price, op.quantity);
        break;
    case BookOpType::Modify:
        book.modify_order(op.ref, op.price, op.quantity);
        break;
    case BookOpType::Clear:
        book.clear();
        break;
    }
}

inline std::uint64_t observe(std::uint64_t hash, std::optional<Price> bid, std::optional<Price> ask)
{
    hash = (hash ^ static_cast<std::uint64_t>(bid.value_or(-1))) * 0x100000001B3ull;
    return (hash ^ static_cast<std::uint64_t>(ask.value_or(-1))) * 0x100000001B3ull;
}

template <typename B>
std::uint64_t final_signature(const B& book, std::size_t instruments)
{
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (InstrumentId instrument = 0; instrument < instruments; ++instrument) {
        const auto security_book = book.find(instrument);
        if (!security_book) {
            continue;
        }
        const auto& resting = **security_book;
        hash = observe(hash, static_cast<Price>(resting.resting_orders()), std::nullopt);
        for (const auto& depth : {resting.bid_depth(10), resting.ask_depth(10)}) {
            for (const LevelSummary& level : depth) {
                hash = observe(hash, level.price, level.total_shares);
            }
        }
    }
    return hash;
}

double ms_between(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

struct ThroughputResult {
    double ms;
    double teardown_ms;
    std::uint64_t checksum;
};

template <typename B>
ThroughputResult run_throughput(const OrderFlow& flow)
{
    auto books = std::make_unique<B>();
    for (std::size_t instrument = 0; instrument < flow.ticks.size(); ++instrument) {
        books->set_tick(static_cast<InstrumentId>(instrument), flow.ticks[instrument]);
    }
    for (std::size_t i = 0; i < flow.warmup; ++i) {
        apply(books->book_for(flow.ops[i].instrument), flow.ops[i]);
    }
    std::uint64_t hash = 0xcbf29ce484222325ull;
    perf_counters().enable();
    const auto start = Clock::now();
    for (std::size_t i = flow.warmup; i < flow.ops.size(); ++i) {
        const BookOp& op = flow.ops[i];
        auto& book = books->book_for(op.instrument);
        apply(book, op);
        hash = observe(hash, book.best_bid(), book.best_ask());
    }
    const auto end = Clock::now();
    perf_counters().disable();
    hash ^= final_signature(*books, flow.ticks.size());
    const auto teardown_start = Clock::now();
    books.reset();
    const auto teardown_end = Clock::now();
    return {ms_between(start, end), ms_between(teardown_start, teardown_end), hash};
}

class CycleClock {
public:
    static std::uint64_t now()
    {
#if defined(__x86_64__)
        _mm_lfence();
        const std::uint64_t ticks = __rdtsc();
        _mm_lfence();
        return ticks;
#else
        return static_cast<std::uint64_t>(Clock::now().time_since_epoch().count());
#endif
    }

    static double ns_per_tick()
    {
        static const double value = calibrate();
        return value;
    }

private:
    static double calibrate()
    {
        const auto wall_start = Clock::now();
        const std::uint64_t ticks_start = now();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const std::uint64_t ticks_end = now();
        const double ns = std::chrono::duration<double, std::nano>(Clock::now() - wall_start).count();
        return ns / static_cast<double>(ticks_end - ticks_start);
    }
};

using LatencySamples = std::array<std::vector<std::uint32_t>, kBookOpTypes>;

template <typename B>
std::uint64_t run_latency(const OrderFlow& flow, LatencySamples& samples)
{
    for (auto& per_type : samples) {
        per_type.clear();
        per_type.reserve(flow.ops.size() / 2);
    }
    auto books = std::make_unique<B>();
    for (std::size_t instrument = 0; instrument < flow.ticks.size(); ++instrument) {
        books->set_tick(static_cast<InstrumentId>(instrument), flow.ticks[instrument]);
    }
    for (std::size_t i = 0; i < flow.warmup; ++i) {
        apply(books->book_for(flow.ops[i].instrument), flow.ops[i]);
    }
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (std::size_t i = flow.warmup; i < flow.ops.size(); ++i) {
        const BookOp& op = flow.ops[i];
        const std::uint64_t start = CycleClock::now();
        auto& book = books->book_for(op.instrument);
        apply(book, op);
        hash = observe(hash, book.best_bid(), book.best_ask());
        const std::uint64_t ticks = CycleClock::now() - start;
        samples[static_cast<std::size_t>(op.type)].push_back(static_cast<std::uint32_t>(std::min<std::uint64_t>(ticks, UINT32_MAX)));
    }
    return hash ^ final_signature(*books, flow.ticks.size());
}

double clock_overhead_ns()
{
    std::vector<std::uint64_t> deltas(100'000);
    for (auto& delta : deltas) {
        const std::uint64_t start = CycleClock::now();
        delta = CycleClock::now() - start;
    }
    std::sort(deltas.begin(), deltas.end());
    return static_cast<double>(deltas[deltas.size() / 2]) * CycleClock::ns_per_tick();
}

template <typename F>
bool with_book(const std::string& name, F&& run)
{
    bool found = false;
    for_each_book_variant([&](auto tag, const BookVariantInfo& info) {
        if (!found && info.name == name) {
            found = true;
            run(tag);
        }
    });
    return found;
}

std::string label(const std::string& scenario, std::string_view impl)
{
    std::string row = scenario + "," + std::string(impl);
    for_each_book_variant([&](auto, const BookVariantInfo& info) {
        if (info.name == impl) {
            row += "," + std::string(info.levels) + "," + std::string(info.index) + "," + std::string(info.orders) +
                   "," + std::to_string(info.cache_slots);
        }
    });
    return row;
}

struct ChecksumCheck {
    std::optional<std::uint64_t> expected;
    bool ok = true;

    void see(const std::string& impl, std::uint64_t checksum)
    {
        if (!expected) {
            expected = checksum;
        } else if (*expected != checksum) {
            std::cerr << "book_bench: " << impl << " built a different book (checksum " << checksum << " vs "
                      << *expected << ")\n";
            ok = false;
        }
    }
};

int throughput(const Options& options, const OrderFlow& flow)
{
    const std::size_t timed_ops = flow.ops.size() - flow.warmup;
    if (options.header) {
        std::cout << "scenario,impl,levels,index,orders,cache,run,ops,ms,mops,ns_per_op,teardown_ms,checksum\n";
    }
    ChecksumCheck check;
    for (int run = 0; run < options.runs; ++run) {
        for (std::size_t k = 0; k < options.impls.size(); ++k) {
            const std::string& impl = options.impls[(static_cast<std::size_t>(run) + k) % options.impls.size()];
            ThroughputResult result{};
            try {
                if (!with_book(impl, [&]<typename B>(std::type_identity<B>) {
                        result = run_throughput<B>(flow);
                    })) {
                    std::cerr << "book_bench: unknown impl " << impl << '\n';
                    return 1;
                }
            } catch (const std::length_error& error) {
                std::cerr << "book_bench: " << impl << " skipped: " << error.what() << '\n';
                continue;
            }
            check.see(impl, result.checksum);
            std::cout << label(options.scenario, impl) << ',' << run << ',' << timed_ops << ',' << result.ms << ','
                      << static_cast<double>(timed_ops) / result.ms / 1000.0 << ','
                      << result.ms * 1e6 / static_cast<double>(timed_ops) << ',' << result.teardown_ms << ','
                      << result.checksum << '\n';
            std::cout.flush();
        }
    }
    return check.ok ? 0 : 2;
}

int latency(const Options& options, const OrderFlow& flow)
{
    const double ns_per_tick = CycleClock::ns_per_tick();
    if (options.header) {
        std::cout << "scenario,impl,levels,index,orders,cache,run,op,count,p50_ns,p90_ns,p99_ns,p999_ns,max_ns,mean_ns,"
                     "clock_overhead_ns\n";
    }
    const double overhead = clock_overhead_ns();
    ChecksumCheck check;
    LatencySamples samples;
    for (int run = 0; run < options.runs; ++run) {
        for (std::size_t k = 0; k < options.impls.size(); ++k) {
            const std::string& impl = options.impls[(static_cast<std::size_t>(run) + k) % options.impls.size()];
            std::uint64_t checksum = 0;
            try {
                if (!with_book(impl, [&]<typename B>(std::type_identity<B>) {
                        checksum = run_latency<B>(flow, samples);
                    })) {
                    std::cerr << "book_bench: unknown impl " << impl << '\n';
                    return 1;
                }
            } catch (const std::length_error& error) {
                std::cerr << "book_bench: " << impl << " skipped: " << error.what() << '\n';
                continue;
            }
            check.see(impl, checksum);
            std::vector<std::uint32_t> all;
            const auto emit = [&](std::string_view op, std::vector<std::uint32_t>& ticks) {
                if (ticks.empty()) {
                    return;
                }
                std::sort(ticks.begin(), ticks.end());
                const auto at = [&](double p) {
                    return static_cast<double>(ticks[static_cast<std::size_t>(p * static_cast<double>(ticks.size() - 1))]) *
                           ns_per_tick;
                };
                double sum = 0;
                for (const std::uint32_t t : ticks) {
                    sum += static_cast<double>(t);
                }
                std::cout << label(options.scenario, impl) << ',' << run << ',' << op << ',' << ticks.size() << ','
                          << at(0.5) << ',' << at(0.9) << ',' << at(0.99) << ',' << at(0.999) << ','
                          << static_cast<double>(ticks.back()) * ns_per_tick << ','
                          << sum / static_cast<double>(ticks.size()) * ns_per_tick << ',' << overhead << '\n';
            };
            for (std::size_t type = 0; type < kBookOpTypes; ++type) {
                all.insert(all.end(), samples[type].begin(), samples[type].end());
                emit(book_op_name(static_cast<BookOpType>(type)), samples[type]);
            }
            emit("all", all);
            std::cout.flush();
        }
    }
    return check.ok ? 0 : 2;
}

int describe(const OrderFlow& flow)
{
    std::array<std::size_t, kBookOpTypes> counts{};
    for (std::size_t i = flow.warmup; i < flow.ops.size(); ++i) {
        ++counts[static_cast<std::size_t>(flow.ops[i].type)];
    }
    std::cout << "instruments=" << flow.ticks.size() << " warmup_ops=" << flow.warmup
              << " timed_ops=" << flow.ops.size() - flow.warmup << " peak_resting=" << flow.peak_resting << '\n';
    for (std::size_t type = 0; type < kBookOpTypes; ++type) {
        std::cout << "  " << book_op_name(static_cast<BookOpType>(type)) << '=' << counts[type] << '\n';
    }
    LadderSecurityBook probe(0);
    probe.set_tick(flow.ticks[0]);
    std::map<std::size_t, std::size_t> distance_histogram;
    std::size_t timed = 0;
    std::size_t bbo_changes = 0;
    std::size_t touch_orders = 0;
    for (std::size_t i = 0; i < flow.ops.size(); ++i) {
        const BookOp& op = flow.ops[i];
        if (op.instrument != 0) {
            continue;
        }
        const auto bid = probe.best_bid();
        const auto ask = probe.best_ask();
        if (i >= flow.warmup && op.type == BookOpType::Add) {
            const auto best = op.side == Side::BID ? bid : ask;
            if (best) {
                const Price ticks = (op.side == Side::BID ? *best - op.price : op.price - *best) / flow.ticks[0];
                ++distance_histogram[static_cast<std::size_t>(std::clamp<Price>(ticks, -1, 20) + 1)];
            }
        }
        apply(probe, op);
        if (i >= flow.warmup) {
            ++timed;
            bbo_changes += probe.best_bid() != bid || probe.best_ask() != ask;
            for (const Side side : {Side::BID, Side::ASK}) {
                const auto top = side == Side::BID ? probe.bid_depth(1) : probe.ask_depth(1);
                touch_orders += top.empty() ? 0 : top[0].total_shares / 550;
            }
        }
    }
    std::cout << "instrument 0: levels bid=" << probe.bid_depth(SIZE_MAX).size()
              << " ask=" << probe.ask_depth(SIZE_MAX).size() << " orders=" << probe.resting_orders()
              << " bbo_changed_on=" << 100.0 * static_cast<double>(bbo_changes) / static_cast<double>(std::max<std::size_t>(timed, 1))
              << "% of ops, ~orders at touch per side=" << static_cast<double>(touch_orders) / static_cast<double>(std::max<std::size_t>(timed, 1)) / 2
              << '\n';
    std::cout << "  add distance from own best in ticks (-1 = improves, 20 = 20+):";
    for (const auto& [bucket, count] : distance_histogram) {
        std::cout << ' ' << static_cast<long>(bucket) - 1 << ':' << count;
    }
    std::cout << '\n';
    return 0;
}

void usage()
{
    std::cerr << "usage: book_bench <mode> [options]\n"
                 "modes:\n"
                 "  list                    print the book implementations\n"
                 "  generate --out FILE     generate a synthetic flow and save it\n"
                 "  describe                print what a flow looks like\n"
                 "  throughput              time each implementation over the flow\n"
                 "  latency                 time every operation with the cycle counter\n"
                 "flow source (default: generate in memory):\n"
                 "  --flow FILE             a flow saved by generate\n"
                 "  --itch FILE             an ITCH 5.0 BinaryFILE\n"
                 "generator:\n"
                 "  --ops N --instruments N --resting N --seed N --random-ids\n"
                 "  --near-touch P          geometric p for distance from the fair price (default 0.1)\n"
                 "  --move P                chance per step that the fair price moves a tick and sweeps (default 0.02)\n"
                 "  --recent-cancel P       share of cancels/replaces aimed at the 64 newest orders (default 0.85)\n"
                 "  --far-fraction F --far-ticks N   share of adds placed 20..N ticks away\n"
                 "run:\n"
                 "  --impl a,b|all --runs N --scenario NAME --no-header\n";
}

}  // namespace

int main(int argc, char* argv[])
{
    try {
        const auto options = parse(argc, argv);
        if (!options) {
            usage();
            return 1;
        }
        if (options->mode == "list") {
            for (const std::string& name : book_variant_names()) {
                std::cout << label("", name).substr(1) << '\n';
            }
            return 0;
        }
        const OrderFlow flow = load_flow(*options);
        if (options->mode == "generate") {
            if (options->out_path.empty()) {
                std::cerr << "book_bench: generate needs --out\n";
                return 1;
            }
            save_order_flow(flow, options->out_path);
            return describe(flow);
        }
        if (options->mode == "describe") {
            return describe(flow);
        }
        if (options->mode == "throughput") {
            return throughput(*options, flow);
        }
        if (options->mode == "latency") {
            return latency(*options, flow);
        }
        usage();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "book_bench: " << error.what() << '\n';
        return 1;
    }
}
