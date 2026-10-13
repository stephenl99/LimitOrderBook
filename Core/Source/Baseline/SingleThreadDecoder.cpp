#include "Baseline/SingleThreadDecoder.h"

#include <fstream>

#include "Message/Itch/DecodeMessage.h"
#include "Message/Itch/HandleMessage.h"

std::expected<std::unique_ptr<Book>, std::string> SingleThreadDecoder::decode_file(const std::filesystem::path& input_path)
{
    std::ifstream input_stream(input_path, std::ios::binary);
    if (!input_stream) {
        return std::unexpected(std::string("decode_file: cannot open ") + input_path.string());
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

        itch::handle_message(itch::decode_message(data), book.get());
    }
    return book;
}
