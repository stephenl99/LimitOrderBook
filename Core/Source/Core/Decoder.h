//
// Created by Stephen Linder on 8/25/26.
//

#pragma once

#include <filesystem>
#include <memory>

#include "Book.h"

namespace fs = std::filesystem;

namespace Decoder {
    std::unique_ptr<Book> decode_file(const fs::path& input_path);
};
