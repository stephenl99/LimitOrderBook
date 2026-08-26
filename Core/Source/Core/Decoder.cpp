//
// Created by Stephen Linder on 8/25/26.
//
#include "Decoder.h"

#include <fstream>
#include <iostream>

#include "Book.h"
#include "Helpers.h"
#include "Order.h"

namespace Reader {
    static uint64_t read_timestamp_ns(const std::vector<uint8_t>& data, size_t offset)
    {
        return Helpers::convert<uint64_t, uint8_t>(
            const_cast<uint8_t*>(data.data() + offset), 6);
    }
}

std::unique_ptr<Book> Decoder::decode_file(const fs::path& input_path)
{
    std::ifstream input_stream(input_path, std::ios::binary);
    if (!input_stream) {
        std::cerr << "decode_file: cannot open " << input_path << "\n";
        return nullptr;
    }

    auto book = std::make_unique<Book>();

    while (true) {
        uint8_t len_buf[2]{};
        input_stream.read(reinterpret_cast<char*>(len_buf), 2);
        if (input_stream.gcount() != 2) {
            break;
        }

        const auto len = static_cast<uint16_t>((len_buf[0] << 8) | len_buf[1]);
        if (len == 0) {
            break;
        }

        std::vector<uint8_t> data(len);
        input_stream.read(reinterpret_cast<char*>(data.data()), len);
        if (input_stream.gcount() != static_cast<std::streamsize>(len)) {
            break;
        }

        if (data[0] != 'A') {
            continue;  // slice 1: only Add Order
        }

        if (auto order = parse_into_order(data); order.has_value()) {
            book->insert(order.value());
        }
    }

    return book;
}

std::optional<Order> Decoder::parse_into_order(const std::vector<uint8_t>& data)
{
    if (data.size() < 36 || data[0] != 'A') {
        return {};
    }

    const uint64_t timestamp_ns = Reader::read_timestamp_ns(data, 5);
    const uint64_t order_reference_number =
        Helpers::convert<uint64_t, uint8_t>(const_cast<uint8_t*>(&data[11]), 8);
    const char side = static_cast<char>(data[19]);
    const uint32_t quantity =
        Helpers::convert<uint32_t, uint8_t>(const_cast<uint8_t*>(&data[20]), 4);
    const uint32_t price =
        Helpers::convert<uint32_t, uint8_t>(const_cast<uint8_t*>(&data[32]), 4);

    return {Order(timestamp_ns, order_reference_number, side, price, quantity)};
}
