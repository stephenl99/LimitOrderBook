//
// Created by Stephen Linder on 8/25/26.
//
#include "Decoder.h"

#include <fstream>
#include <iostream>

#include "Itch/AddOrderMessage.h"
#include "Itch/DecodeMessage.h"
#include "Itch/OrderCancelMessage.h"
#include "Itch/OrderDeleteMessage.h"
#include "Itch/OrderExecutedMessage.h"
#include "Itch/OrderExecutedWithPriceMessage.h"
#include "Itch/OrderReplaceMessage.h"
#include "Itch/StockDirectoryMessage.h"
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

        const uint16_t stock_locate = message->stock_locate();
        SecurityBook& security_book = book->book_for(stock_locate);

        switch (message->message_type()) {
        case 'R': {
            const auto& directory_msg = dynamic_cast<const itch::StockDirectoryMessage&>(*message);
            book->directory.add(stock_locate, directory_msg.stock());
            break;
        }
        case 'A':
        case 'F': {
            const auto& add = dynamic_cast<const itch::AddOrderMessage&>(*message);
            Order order(add.timestamp_ns(),
                        stock_locate,
                        add.order_reference_number(),
                        add.side(),
                        add.price(),
                        add.shares());
            security_book.insert(order);
            break;
        }
        case 'P':
        case 'Q':
            // Prints — not displayed-book updates.
            break;
        case 'D': {
            const auto& to_delete = dynamic_cast<const itch::OrderDeleteMessage&>(*message);
            security_book.delete_order(to_delete.order_reference_number());
            break;
        }
        case 'E': {
            const auto& to_execute = dynamic_cast<const itch::OrderExecutedMessage&>(*message);
            security_book.execute_order(to_execute.order_reference_number(), to_execute.executed_shares());
            break;
        }
        case 'C': {
            const auto& to_execute = dynamic_cast<const itch::OrderExecutedWithPriceMessage&>(*message);
            security_book.execute_order(to_execute.order_reference_number(), to_execute.executed_shares());
            break;
        }
        case 'X': {
            const auto& to_cancel = dynamic_cast<const itch::OrderCancelMessage&>(*message);
            security_book.cancel_order(to_cancel.order_reference_number(), to_cancel.cancelled_shares());
            break;
        }
        case 'U': {
            const auto& to_replace = dynamic_cast<const itch::OrderReplaceMessage&>(*message);
            security_book.replace_order(to_replace.original_order_reference(),
                               to_replace.new_order_reference(),
                               to_replace.price(),
                               to_replace.shares());
            break;
        }
        default:
            break;
        }
    }

    return book;
}
