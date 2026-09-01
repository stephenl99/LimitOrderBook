//
// Created by Stephen Linder on 8/25/26.
//

#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include "Book.h"

namespace fs = std::filesystem;

namespace Decoder {
    std::expected<std::unique_ptr<Book>, std::string> decode_file(const fs::path& input_path);
}
