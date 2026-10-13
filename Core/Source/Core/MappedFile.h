#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

class MappedFile {
public:
    static std::expected<MappedFile, std::string> open(const std::filesystem::path& path);

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    ~MappedFile();

    std::span<const uint8_t> bytes() const { return {data_, size_}; }

private:
    MappedFile(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    void release();

    const uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
};
