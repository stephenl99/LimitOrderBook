#include <chrono>
#include <cstdlib>
#include <databento/dbn_store.hpp>
#include <databento/exceptions.hpp>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

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

}  // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "usage: databento_replay <file.dbn.zst> [label] [print_every_seconds] [depth.csv] [levels] [speed] [single|ring]\n"
                  << "  speed 0 replays as fast as possible, 1 at market pace, 60 at one market minute per second\n";
        return 1;
    }

    const std::filesystem::path path = argv[1];
    const std::string label = argc > 2 ? argv[2] : path.stem().string(); // where to print the results to
    const int print_every_seconds = argc > 3 ? std::atoi(argv[3]) : 60;
    const std::filesystem::path depth_path = argc > 4 ? argv[4] : "";
    const std::size_t depth_levels = argc > 5 ? static_cast<std::size_t>(std::atoi(argv[5])) : 40;
    const double speed = argc > 6 ? std::atof(argv[6]) : 0.0;
    const Threading threading = argc > 7 && std::string(argv[7]) == "ring" ? Threading::Pipeline : Threading::Single;

    if (!std::filesystem::exists(path)) {
        std::cerr << "databento_replay: no such file " << path << " (run make mbo_download first)\n";
        return 1;
    }

    try {
        std::ofstream depth_file;
        MboSession session(label, std::chrono::seconds{print_every_seconds}, threading);
        if (!depth_path.empty()) {
            depth_file.open(depth_path);
            if (!depth_file) {
                std::cerr << "databento_replay: cannot write " << depth_path << '\n';
                return 1;
            }
            session.log_depth_to(depth_file, depth_levels);
        }
        db::DbnStore store{path};
        MarketClock clock(speed);

        const auto start = std::chrono::steady_clock::now();
        store.Replay([&](const db::Record& rec) {
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
        if (speed > 0.0) {
            std::cout << " (paced at " << speed << "x market speed)\n";
        } else {
            std::cout << " (" << static_cast<std::uint64_t>(static_cast<double>(session.records()) / seconds)
                      << " msgs/s, " << (threading == Threading::Pipeline ? "ring pipeline" : "single thread") << ")\n";
        }
        return session.records() == 0 ? 1 : 0;
    } catch (const db::Exception& e) {
        std::cerr << "databento error: " << e.what() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
    return 1;
}
