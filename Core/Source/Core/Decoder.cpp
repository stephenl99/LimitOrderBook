//
// Created by Stephen Linder on 8/25/26.
//
#include "Decoder.h"

#include <fstream>
#include <iostream>

#include "Itch/AddOrderMessage.h"
#include "Itch/DecodeMessage.h"
#include "Order.h"

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

        std::unique_ptr<itch::Message> message = itch::decode_message(data);
        if (message == nullptr) {
            continue;
        }

        switch (message->message_type()) {
        case 'A':
        case 'F': {
            const auto& add = static_cast<const itch::AddOrderMessage&>(*message);
            Order order(add.timestamp_ns(), add.order_reference_number(), add.side(),
                        add.price(), add.shares());
            book->insert(order);
            break;
        }
        case 'P':
        case 'Q':
            // Prints — not displayed-book updates.
            break;
        default:
            // D/E/C/X/U — wire to Book when you implement those handlers.
            break;
        }
    }

    return book;
}
