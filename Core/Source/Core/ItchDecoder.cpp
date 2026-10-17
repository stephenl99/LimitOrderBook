//
// Created by Stephen Linder on 8/25/26.
//
#include "ItchDecoder.h"

#include <iostream>
#include <thread>

#include "Logger.h"
#include "MappedFile.h"
#include "Handoff/MessagePipeline.h"
#include "Message/Itch/DecodeMessage.h"
#include "Message/Itch/FrameReader.h"
#include "Message/Itch/HandleMessage.h"

namespace {

constexpr std::size_t kQueueCapacity = 1 << 16;

}  // namespace

std::expected<std::unique_ptr<ItchBook>, std::string> ItchDecoder::decode_file(const fs::path& input_path)
{
    const auto file = MappedFile::open(input_path);
    if (!file) {
        return std::unexpected("decode_file: " + file.error());
    }

    auto book = std::make_unique<ItchBook>();
    auto pipeline = make_pipeline<std::unique_ptr<itch::Message>, kQueueCapacity>(
        [&book](std::unique_ptr<itch::Message>& message) { itch::handle_message(message, book.get()); });

    std::thread parse_thread([&] {
        itch::FrameReader frames(file->bytes());
        while (const auto frame = frames.next()) {
            if (auto message = itch::decode_message(*frame)) {
                pipeline.push(std::move(message));
            }
        }
    });
    parse_thread.join();
    pipeline.finish();
    return book;
}
