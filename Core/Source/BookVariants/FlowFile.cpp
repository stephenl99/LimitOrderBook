#include "BookVariants/OrderFlow.h"

#include <array>
#include <fstream>
#include <stdexcept>

namespace {

constexpr std::uint64_t kFlowMagic = 0x31574f4c464b4f42ull;

}  // namespace

void save_order_flow(const OrderFlow& flow, const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::binary);
    const std::array<std::uint64_t, 5> header = {kFlowMagic, flow.warmup, flow.peak_resting, flow.ticks.size(),
                                                 flow.ops.size()};
    out.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
    out.write(reinterpret_cast<const char*>(flow.ticks.data()),
              static_cast<std::streamsize>(flow.ticks.size() * sizeof(Price)));
    out.write(reinterpret_cast<const char*>(flow.ops.data()),
              static_cast<std::streamsize>(flow.ops.size() * sizeof(BookOp)));
    if (!out) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

OrderFlow read_order_flow(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::array<std::uint64_t, 5> header{};
    in.read(reinterpret_cast<char*>(header.data()), sizeof(header));
    if (!in || header[0] != kFlowMagic) {
        throw std::runtime_error(path.string() + " is not a book_bench flow file");
    }
    OrderFlow flow;
    flow.warmup = header[1];
    flow.peak_resting = header[2];
    flow.ticks.resize(header[3]);
    flow.ops.resize(header[4]);
    in.read(reinterpret_cast<char*>(flow.ticks.data()), static_cast<std::streamsize>(flow.ticks.size() * sizeof(Price)));
    in.read(reinterpret_cast<char*>(flow.ops.data()), static_cast<std::streamsize>(flow.ops.size() * sizeof(BookOp)));
    if (!in) {
        throw std::runtime_error(path.string() + " is truncated");
    }
    return flow;
}
