#include <chrono>
#include <cstdlib>
#include <databento/exceptions.hpp>
#include <databento/live.hpp>
#include <databento/live_threaded.hpp>
#include <exception>
#include <iostream>
#include <thread>

#include "MboSession.h"

namespace db = databento;

namespace {

int run(const std::string& symbol, int seconds)
{
    MboSession session(symbol, std::chrono::milliseconds{250});

    {
        auto client = db::LiveThreaded::Builder()
                          .SetKeyFromEnv()
                          .SetDataset(db::Dataset::GlbxMdp3)
                          .BuildThreaded();

        std::cout << "subscribing to " << symbol << " MBO on GLBX.MDP3 with snapshot, " << seconds << "s\n";
        client.SubscribeWithSnapshot({symbol}, db::Schema::Mbo, db::SType::RawSymbol);
        client.Start([&](const db::Record& rec) {
            session.on_record(rec);
            return db::KeepGoing::Continue;
        });
        std::this_thread::sleep_for(std::chrono::seconds{seconds});
    }

    session.print_summary();
    return session.records() == 0 ? 1 : 0;
}

}  // namespace

int main(int argc, char* argv[])
{
    const std::string symbol = argc > 1 ? argv[1] : "ESZ6";
    const int seconds = argc > 2 ? std::atoi(argv[2]) : 15;

    if (std::getenv("DATABENTO_API_KEY") == nullptr) {
        std::cerr << "DATABENTO_API_KEY is not set. export it in this shell, then rerun.\n";
        return 1;
    }

    try {
        return run(symbol, seconds);
    } catch (const db::Exception& e) {
        std::cerr << "databento error: " << e.what() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
    return 1;
}
