#include "MappedFile.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <utility>

std::expected<MappedFile, std::string> MappedFile::open(const std::filesystem::path& path)
{
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return std::unexpected("cannot open " + path.string());
    }

    struct stat info {};
    if (::fstat(fd, &info) != 0) {
        ::close(fd);
        return std::unexpected("cannot stat " + path.string());
    }

    const auto size = static_cast<std::size_t>(info.st_size);
    if (size == 0) {
        ::close(fd);
        return MappedFile(nullptr, 0);
    }

    void* mapping = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapping == MAP_FAILED) {
        return std::unexpected("cannot map " + path.string());
    }
    ::madvise(mapping, size, MADV_SEQUENTIAL);
    return MappedFile(static_cast<const uint8_t*>(mapping), size);
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0))
{
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept
{
    if (this != &other) {
        release();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

MappedFile::~MappedFile()
{
    release();
}

void MappedFile::release()
{
    if (data_ != nullptr) {
        ::munmap(const_cast<uint8_t*>(data_), size_);
        data_ = nullptr;
        size_ = 0;
    }
}
