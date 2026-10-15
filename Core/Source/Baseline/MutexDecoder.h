#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include "Book/Book.h"

namespace MutexDecoder {
    std::expected<std::unique_ptr<Book>, std::string> decode_file(const std::filesystem::path& input_path);
}
