//
// Created by Stephen Linder on 8/25/26.
//
#include "Decoder.h"

#include <fstream>
#include <iostream>

#include "Book.h"
#include "Helpers.h"
#include "Order.h"

namespace {

uint64_t ReadTimestampNs(const std::vector<uint8_t>& data, size_t offset)
{
    return Helpers::convert<uint64_t, uint8_t>(
        const_cast<uint8_t*>(data.data() + offset), 6);
}

}  // namespace

Book* Decoder::decodeFile(const fs::path& inputPath)
{
    std::ifstream inputStream(inputPath, std::ios::binary);
    if (!inputStream) {
        std::cerr << "decodeFile: cannot open " << inputPath << "\n";
        return nullptr;
    }

    auto* book = new Book();

    // Do not use while (!eof()) — eof is set only after a failed read.
    while (true) {
        uint8_t len_buf[2]{};
        inputStream.read(reinterpret_cast<char*>(len_buf), 2);
        if (inputStream.gcount() != 2) {
            break;
        }

        const uint16_t len = static_cast<uint16_t>((len_buf[0] << 8) | len_buf[1]);
        if (len == 0) {
            break;
        }

        std::vector<uint8_t> data(len);
        inputStream.read(reinterpret_cast<char*>(data.data()), len);
        if (inputStream.gcount() != static_cast<std::streamsize>(len)) {
            break;
        }

        if (data[0] != 'A') {
            continue;  // slice 1: only Add Order
        }

        Order* order = parseIntoOrder(data);
        if (order != nullptr) {
            book->insert(order);
        }
    }

    return book;
}

Order* Decoder::parseIntoOrder(const std::vector<uint8_t>& data)
{
    if (data.size() < 36 || data[0] != 'A') {
        return nullptr;
    }

    const uint64_t timestamp_ns = ReadTimestampNs(data, 5);
    const uint64_t order_reference_number =
        Helpers::convert<uint64_t, uint8_t>(const_cast<uint8_t*>(&data[11]), 8);
    const char side = static_cast<char>(data[19]);
    const uint32_t quantity =
        Helpers::convert<uint32_t, uint8_t>(const_cast<uint8_t*>(&data[20]), 4);
    const uint32_t price =
        Helpers::convert<uint32_t, uint8_t>(const_cast<uint8_t*>(&data[32]), 4);

    return new Order(timestamp_ns, order_reference_number, side, price, quantity);
}
