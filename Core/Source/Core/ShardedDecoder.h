#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include "Book/Book.h"

namespace ShardedDecoder {
    std::expected<std::unique_ptr<Book>, std::string> decode_file(const std::filesystem::path& input_path, std::size_t shards);
}
