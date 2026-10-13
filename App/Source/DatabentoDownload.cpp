#include <cstdlib>
#include <databento/exceptions.hpp>
#include <databento/historical.hpp>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace db = databento;

int main(int argc, char* argv[])
{
    if (argc < 5) {
        std::cerr << "usage: databento_download <symbol> <start> <end> <out.dbn.zst> [--confirm]\n"
                  << "  times are UTC, e.g. 2026-10-08T00:00\n"
                  << "  without --confirm this only prints the cost\n";
        return 1;
    }

    const std::string dataset = "GLBX.MDP3";
    const std::vector<std::string> symbols = {argv[1]};
    const db::DateTimeRange<std::string> range{argv[2], argv[3]};
    const std::filesystem::path out = argv[4];
    const bool confirmed = argc > 5 && std::string(argv[5]) == "--confirm";

    if (std::getenv("DATABENTO_API_KEY") == nullptr) {
        std::cerr << "DATABENTO_API_KEY is not set. export it in this shell, then rerun.\n";
        return 1;
    }

    try {
        auto client = db::Historical::Builder().SetKeyFromEnv().Build();

        const std::uint64_t records = client.MetadataGetRecordCount(dataset, range, symbols, db::Schema::Mbo);
        const std::uint64_t bytes = client.MetadataGetBillableSize(dataset, range, symbols, db::Schema::Mbo);
        const double cost = client.MetadataGetCost(dataset, range, symbols, db::Schema::Mbo);

        std::cout << symbols.front() << " MBO " << range.start << " to " << range.end << " UTC\n"
                  << "records:       " << records << '\n'
                  << "billable size: " << std::fixed << std::setprecision(1)
                  << static_cast<double>(bytes) / 1e6 << " MB\n"
                  << "cost:          $" << std::setprecision(4) << cost << '\n';

        if (!confirmed) {
            std::cout << "nothing downloaded. rerun with --confirm (make mbo_download) to fetch it.\n";
            return 0;
        }

        std::filesystem::create_directories(out.parent_path());
        client.TimeseriesGetRangeToFile(dataset, range, symbols, db::Schema::Mbo, out);
        std::cout << "wrote " << out.string() << " (" << std::filesystem::file_size(out) / 1'000'000 << " MB on disk)\n";
        return 0;
    } catch (const db::Exception& e) {
        std::cerr << "databento error: " << e.what() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
    return 1;
}
