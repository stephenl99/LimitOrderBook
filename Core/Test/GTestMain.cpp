#include "Core/Book.h"
#include "Core/Decoder.h"
#include "Itch/AddOrderMessage.h"
#include "Itch/DecodeMessage.h"
#include "Itch/OrderDeleteMessage.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace {

std::filesystem::path fixture(const char* name)
{
    // Tests run with cwd = build/ or repo root depending on meson; try both.
    const std::filesystem::path candidates[] = {
        std::filesystem::path("testdata") / name,
        std::filesystem::path("..") / "testdata" / name,
    };
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p;
        }
    }
    return candidates[0];
}

std::vector<uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in) << "missing " << path;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

TEST(DecodeMessage, AddOrderFixture)
{
    const auto bytes = read_file(fixture("add_order_a.bin"));
    ASSERT_GE(bytes.size(), 2u);
    const auto len = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
    ASSERT_EQ(len, 36u);
    ASSERT_EQ(bytes.size(), 2u + len);

    std::vector<uint8_t> payload(bytes.begin() + 2, bytes.end());
    auto msg = itch::decode_message(payload);
    ASSERT_NE(msg, nullptr);
    ASSERT_EQ(msg->message_type(), 'A');

    const auto& add = static_cast<const itch::AddOrderMessage&>(*msg);
    EXPECT_EQ(add.order_reference_number(), 12345u);
    EXPECT_EQ(add.side(), 'B');
    EXPECT_EQ(add.shares(), 100u);
    EXPECT_EQ(add.price(), 1500000u);
    EXPECT_EQ(add.timestamp_ns(), 1000000000u);
}

TEST(DecodeMessage, DeletePayload)
{
    // Minimal D: type + locate + track + ts + ref 12345
    std::vector<uint8_t> data(19);
    data[0] = 'D';
    data[1] = 0;
    data[2] = 1;  // locate 1
    // tracking 0, ts 0 already zeroed
    const uint64_t ref = 12345;
    for (int i = 0; i < 8; ++i) {
        data[11 + i] = static_cast<uint8_t>((ref >> (56 - 8 * i)) & 0xff);
    }

    auto msg = itch::decode_message(data);
    ASSERT_NE(msg, nullptr);
    const auto& del = static_cast<const itch::OrderDeleteMessage&>(*msg);
    EXPECT_EQ(del.order_reference_number(), 12345u);
}

TEST(Book, InsertAddCreatesBidLevel)
{
    Order order(1'000'000'000ull, 12345ull, 'B', 1'500'000u, 100u);
    Book book;
    book.insert(order);
    EXPECT_EQ(book.bid_levels.size(), 1u);
    EXPECT_EQ(book.ask_levels.size(), 0u);
    ASSERT_FALSE(book.bid_levels.begin()->second.orders.empty());
    EXPECT_EQ(book.bid_levels.begin()->second.orders.front().order_reference_number(), 12345u);
}

TEST(Decoder, SessionMixAppliesAdds)
{
    Decoder decoder;
    auto book = decoder.decode_file(fixture("session_mix.bin"));
    ASSERT_NE(book, nullptr);
    EXPECT_EQ(book->bid_levels.size(), 3u);  // 150.00, 149.99, 149.95 (E/D not applied yet)
    EXPECT_EQ(book->ask_levels.size(), 2u);  // 150.05, 150.10
}

TEST(Decoder, TinyAddFixture)
{
    Decoder decoder;
    auto book = decoder.decode_file(fixture("add_order_a.bin"));
    ASSERT_NE(book, nullptr);
    EXPECT_EQ(book->bid_levels.size(), 1u);
    EXPECT_EQ(book->ask_levels.size(), 0u);
}
