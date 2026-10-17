#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include "Book/OuterBook/ItchBook.h"

namespace ItchShardedDecoder {
    std::expected<std::unique_ptr<ItchBook>, std::string> decode_file(const std::filesystem::path& input_path, std::size_t shards);
}
