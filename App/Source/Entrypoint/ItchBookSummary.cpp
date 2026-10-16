#include <iostream>

#include "Core/ItchDecoder.h"
#include "Core/Logger.h"

namespace {

void print_book_summary(const char* path)
{
    Logger::set_min_level(Logger::Level::Debug);
    auto result = ItchDecoder::decode_file(path);
    if (!result) {
        Logger::warn("Failed to decode: " + result.error());
        return;
    }
    const std::unique_ptr<ItchBook>& books = *result;
    const auto book = books->find(1);
    if (!book.has_value()) {
        std::cout << "core_test: no book for locate 1\n";
        return;
    }
    std::cout << "core_test: decoded " << path << "\n"
              << " locate=" << (*book)->stock_locate() << "\n"
              << " bid_levels=" << (*book)->bid_levels.size() << "\n"
              << " ask_levels=" << (*book)->ask_levels.size() << "\n";
    if (!(*book)->bid_levels.empty()) {
        const Level& level = (*book)->bid_levels.begin()->second;
        if (!level.orders.empty()) {
            std::cout << level.orders.front().order_reference_number() << "\n";
        }
    }

    std::cout << " bids:\n";
    for (const auto& summary : (*book)->bid_depth(5)) {
        std::cout << "  price=" << summary.price << " shares=" << summary.total_shares << "\n";
    }
    std::cout << " asks:\n";
    for (const auto& summary : (*book)->ask_depth(5)) {
        std::cout << "  price=" << summary.price << " shares=" << summary.total_shares << "\n";
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    const char* path = argc > 1 ? argv[1] : "testdata/add_order_a.bin";
    print_book_summary(path);
    return 0;
}
