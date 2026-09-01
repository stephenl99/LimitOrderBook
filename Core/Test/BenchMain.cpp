#include "Core/Book.h"
#include "Core/Decoder.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static std::uint64_t count_frames(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return 0;
    }

    std::uint64_t frames = 0;
    while (true) {
        uint8_t len_buf[2]{};
        input.read(reinterpret_cast<char*>(len_buf), 2);
        if (input.gcount() != 2) {
            break;
        }

        const auto len = static_cast<uint16_t>((len_buf[0] << 8) | len_buf[1]);
        if (len == 0) {
            break;
        }

        input.seekg(len, std::ios::cur);
        if (!input) {
            break;
        }
        ++frames;
    }
    return frames;
}

int main(int argc, char* argv[])
{
    const int repeats = argc > 1 ? std::atoi(argv[1]) : 20;
    const fs::path path = argc > 2 ? argv[2] : "testdata/big_mix.bin";

    if (repeats < 1) {
        std::cerr << "bench: repeats must be >= 1, currently are " << repeats << "\n";
        return 1;
    }

    const std::uint64_t frames = count_frames(path);
    if (frames == 0) {
        std::cerr << "bench: cannot read or empty fixture: " << path << "\n";
        return 1;
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) {
        std::unique_ptr<Book> book = Decoder::decode_file(path);
        if (book == nullptr) {
            std::cerr << "bench: decode_file failed on repeat " << i << "\n";
            return 1;
        }
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double msgs = static_cast<double>(frames) * static_cast<double>(repeats);
    const double msgs_per_sec = msgs / (total_ms / 1000.0);

    std::cout << "frames=" << frames << " repeats=" << repeats << " total_ms=" << total_ms
              << " ms_per_run=" << (total_ms / static_cast<double>(repeats))
              << " msgs_per_sec=" << static_cast<std::uint64_t>(msgs_per_sec) << " path=" << path.string()
              << '\n';

    return 0;
}
