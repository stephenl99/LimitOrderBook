//
// Created by Stephen Linder on 8/25/26.
//
#include "Decoder.h"

#include <atomic>
#include <iostream>
#include <thread>

#include "CircularBuffer.h"
#include "Logger.h"
#include "MappedFile.h"
#include "Message/Itch/DecodeMessage.h"
#include "Message/Itch/FrameReader.h"
#include "Message/Itch/HandleMessage.h"

namespace {

constexpr std::size_t kQueueCapacity = 1 << 16;

}  // namespace

std::expected<std::unique_ptr<Book>, std::string> Decoder::decode_file(const fs::path& input_path)
{
    const auto file = MappedFile::open(input_path);
    if (!file) {
        return std::unexpected("decode_file: " + file.error());
    }

    std::atomic<bool> parsing_done = false;
    auto book = std::make_unique<Book>();
    CircularBuffer<std::unique_ptr<itch::Message>, kQueueCapacity> queue;

    auto parse_function = [&] {
        itch::FrameReader frames(file->bytes());
        while (const auto frame = frames.next()) {
            auto message = itch::decode_message(*frame);
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
