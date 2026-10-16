#include "Baseline/ItchSingleThreadDecoder.h"

#include "Core/MappedFile.h"
#include "Message/Itch/DecodeMessage.h"
#include "Message/Itch/FrameReader.h"
#include "Message/Itch/HandleMessage.h"

std::expected<std::unique_ptr<ItchBook>, std::string> ItchSingleThreadDecoder::decode_file(const std::filesystem::path& input_path)
{
    const auto file = MappedFile::open(input_path);
    if (!file) {
        return std::unexpected("decode_file: " + file.error());
    }

    auto book = std::make_unique<ItchBook>();
    itch::FrameReader frames(file->bytes());
    while (const auto frame = frames.next()) {
        itch::handle_message(itch::decode_message(*frame), book.get());
    }
    return book;
}
