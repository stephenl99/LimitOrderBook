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

int run(const std::string& symbol, int seconds, const std::string& dataset)
{
    MboSession session(symbol, std::chrono::milliseconds{250}, Threading::Pipeline);

    {
        auto client = db::LiveThreaded::Builder()
                          .SetKeyFromEnv()
                          .SetDataset(dataset)
                          .BuildThreaded();

        std::cout << "subscribing to " << symbol << " MBO on " << dataset << " with snapshot, " << seconds << "s\n";
        client.SubscribeWithSnapshot({symbol}, db::Schema::Mbo, db::SType::RawSymbol);
        client.Start([&](const db::Record& rec) {
            session.on_record(rec);
            return db::KeepGoing::Continue;
        });
        std::this_thread::sleep_for(std::chrono::seconds{seconds});
    }
    session.finish();

    session.print_summary();
    return session.records() == 0 ? 1 : 0;
}

}  // namespace

int main(int argc, char* argv[])
{
    const std::string symbol = argc > 1 ? argv[1] : "ESZ6";
    const int seconds = argc > 2 ? std::atoi(argv[2]) : 15;
    const std::string dataset = argc > 3 ? argv[3] : "GLBX.MDP3";

    if (std::getenv("DATABENTO_API_KEY") == nullptr) {
        std::cerr << "DATABENTO_API_KEY is not set. export it in this shell, then rerun.\n";
        return 1;
    }

    try {
        return run(symbol, seconds, dataset);
    } catch (const db::Exception& e) {
        std::cerr << "databento error: " << e.what() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
    return 1;
}
