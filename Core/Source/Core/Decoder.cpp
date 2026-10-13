//
// Created by Stephen Linder on 8/25/26.
//
#include "Decoder.h"

#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>

#include "CircularBuffer.h"
#include "Logger.h"
#include "Itch/DecodeMessage.h"
#include "Itch/HandleMessage.h"

namespace {

constexpr std::size_t kQueueCapacity = 1 << 16;

}  // namespace

std::expected<std::unique_ptr<Book>, std::string> Decoder::decode_file(const fs::path& input_path)
{
    std::ifstream input_stream(input_path, std::ios::binary);
    if (!input_stream) {
        return std::unexpected(std::string("decode_file: cannot open ") + input_path.string());
    }

    std::atomic<bool> parsing_done = false;
    auto book = std::make_unique<Book>();
    CircularBuffer<std::unique_ptr<itch::Message>, kQueueCapacity> queue;

    auto parse_function = [&] {
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

            auto message = itch::decode_message(data);
            if (message == nullptr) {
                continue;
            }
            while (!queue.try_push(std::move(message))) {
                std::this_thread::yield();
            }
        }

        parsing_done.store(true, std::memory_order_release);
    };

    auto handle_function = [&] {
        while (true) {
            if (auto message = queue.try_pop()) {
                itch::handle_message(*message, book.get());
            } else if (parsing_done.load(std::memory_order_acquire)) {
                if (queue.empty()) {
                    break;
                }
            } else {
                std::this_thread::yield();
            }
        }
    };

    std::thread parse_thread(parse_function);
    std::thread action_thread(handle_function);
    parse_thread.join();
    action_thread.join();
    return book;
}
