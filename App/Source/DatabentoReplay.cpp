#include <chrono>
#include <cstdlib>
#include <databento/dbn_store.hpp>
#include <databento/exceptions.hpp>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "MboSession.h"

namespace db = databento;

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "usage: databento_replay <file.dbn.zst> [label] [print_every_seconds] [depth.csv] [levels]\n";
        return 1;
    }

    const std::filesystem::path path = argv[1];
    const std::string label = argc > 2 ? argv[2] : path.stem().string();
    const int print_every_seconds = argc > 3 ? std::atoi(argv[3]) : 60;
    const std::filesystem::path depth_path = argc > 4 ? argv[4] : "";
    const std::size_t depth_levels = argc > 5 ? static_cast<std::size_t>(std::atoi(argv[5])) : 40;

    if (!std::filesystem::exists(path)) {
        std::cerr << "databento_replay: no such file " << path << " (run make mbo_download first)\n";
        return 1;
    }

    try {
        MboSession session(label, std::chrono::seconds{print_every_seconds});
        std::ofstream depth_file;
        if (!depth_path.empty()) {
            depth_file.open(depth_path);
            if (!depth_file) {
                std::cerr << "databento_replay: cannot write " << depth_path << '\n';
                return 1;
            }
            session.log_depth_to(depth_file, depth_levels);
        }
        db::DbnStore store{path};

        const auto start = std::chrono::steady_clock::now();
        store.Replay([&](const db::Record& rec) {
            session.on_record(rec);
            return db::KeepGoing::Continue;
        });
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        session.print_summary();
        std::cout << "replayed " << session.records() << " records in " << seconds << "s ("
                  << static_cast<std::uint64_t>(static_cast<double>(session.records()) / seconds) << " msgs/s)\n";
        return session.records() == 0 ? 1 : 0;
    } catch (const db::Exception& e) {
        std::cerr << "databento error: " << e.what() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
    return 1;
}
