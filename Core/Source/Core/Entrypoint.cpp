//
// Created by Stephen Linder on 8/31/26.
//

#include "Entrypoint.h"

#include <iostream>

#include "Decoder.h"
#include "Logger.h"

void Entrypoint::enter(const char* path) {
    Logger::set_min_level(Logger::Level::Debug);
    auto result = Decoder::decode_file(path);
    if (!result) {
        Logger::warn("Failed to decode: " + result.error());
        return;
    }
    const std::unique_ptr<Book>& books = *result;
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
            std::cout << level.orders.front()->order_reference_number() << "\n";
        }
    }

    std::cout << " bids:\n";
    for (const auto& summary : depth_snapshot((*book)->bid_levels, 5)) {
        std::cout << "  price=" << summary.price << " shares=" << summary.total_shares << "\n";
    }
    std::cout << " asks:\n";
    for (const auto& summary : depth_snapshot((*book)->ask_levels, 5)) {
        std::cout << "  price=" << summary.price << " shares=" << summary.total_shares << "\n";
    }

    std::cout << " bid_vwap=" << vwap((*book)->bid_levels, 5) << "\n"
              << " ask_vwap=" << vwap((*book)->ask_levels, 5) << "\n";
}
