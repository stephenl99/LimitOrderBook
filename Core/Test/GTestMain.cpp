#include "Core/Book.h"
#include "Core/Decoder.h"
#include "Itch/AddOrderMessage.h"
#include "Itch/DecodeMessage.h"
#include "Itch/OrderCancelMessage.h"
#include "Itch/OrderDeleteMessage.h"
#include "Itch/OrderExecutedWithPriceMessage.h"
#include "Itch/OrderReplaceMessage.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
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

std::optional<uint32_t> resting_quantity(const Book& book, uint64_t order_reference_number)
{
    const auto it = book.order_mapping.find(order_reference_number);
    if (it == book.order_mapping.end()) {
        return std::nullopt;
    }
    return it->second->it->quantity();
}

std::optional<uint32_t> level_price_for(const Book& book, uint64_t order_reference_number)
{
    const auto it = book.order_mapping.find(order_reference_number);
    if (it == book.order_mapping.end()) {
        return std::nullopt;
    }
    return it->second->price;
}

void write_be64(std::vector<uint8_t>& data, size_t offset, uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        data[offset + static_cast<size_t>(i)] =
            static_cast<uint8_t>((value >> (56 - 8 * i)) & 0xff);
    }
}

void write_be32(std::vector<uint8_t>& data, size_t offset, uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        data[offset + static_cast<size_t>(i)] =
            static_cast<uint8_t>((value >> (24 - 8 * i)) & 0xff);
    }
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
    std::vector<uint8_t> data(19);
    data[0] = 'D';
    data[1] = 0;
    data[2] = 1;
    write_be64(data, 11, 12345);

    auto msg = itch::decode_message(data);
    ASSERT_NE(msg, nullptr);
    const auto& del = static_cast<const itch::OrderDeleteMessage&>(*msg);
    EXPECT_EQ(del.order_reference_number(), 12345u);
}

TEST(DecodeMessage, CancelPayload)
{
    std::vector<uint8_t> data(23);
    data[0] = 'X';
    write_be64(data, 11, 20002);
    write_be32(data, 19, 50);

    auto msg = itch::decode_message(data);
    ASSERT_NE(msg, nullptr);
    const auto& cancel = static_cast<const itch::OrderCancelMessage&>(*msg);
    EXPECT_EQ(cancel.order_reference_number(), 20002u);
    EXPECT_EQ(cancel.cancelled_shares(), 50u);
}

TEST(DecodeMessage, ReplacePayload)
{
    std::vector<uint8_t> data(35);
    data[0] = 'U';
    write_be64(data, 11, 10004);
    write_be64(data, 19, 10005);
    write_be32(data, 27, 60);
    write_be32(data, 31, 1'500'800);

    auto msg = itch::decode_message(data);
    ASSERT_NE(msg, nullptr);
    const auto& replace = static_cast<const itch::OrderReplaceMessage&>(*msg);
    EXPECT_EQ(replace.original_order_reference(), 10004u);
    EXPECT_EQ(replace.new_order_reference(), 10005u);
    EXPECT_EQ(replace.shares(), 60u);
    EXPECT_EQ(replace.price(), 1'500'800u);
}

TEST(DecodeMessage, ExecutedWithPricePayload)
{
    std::vector<uint8_t> data(36);
    data[0] = 'C';
    write_be64(data, 11, 30003);
    write_be32(data, 19, 25);
    write_be64(data, 23, 9009);
    data[31] = 'Y';
    write_be32(data, 32, 1'500'100);

    auto msg = itch::decode_message(data);
    ASSERT_NE(msg, nullptr);
    const auto& executed = static_cast<const itch::OrderExecutedWithPriceMessage&>(*msg);
    EXPECT_EQ(executed.order_reference_number(), 30003u);
    EXPECT_EQ(executed.executed_shares(), 25u);
    EXPECT_EQ(executed.match_number(), 9009u);
    EXPECT_EQ(executed.printable(), 'Y');
    EXPECT_EQ(executed.execution_price(), 1'500'100u);
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
    EXPECT_EQ(resting_quantity(book, 12345), 100u);
}

TEST(Book, DeleteOrderRemovesRestingOrder)
{
    Order order(1'000'000'000ull, 5001ull, 'B', 1'500'000u, 100u);
    Book book;
    book.insert(order);
    ASSERT_TRUE(book.order_mapping.contains(5001));

    book.delete_order(5001);

    EXPECT_EQ(book.bid_levels.size(), 0u);
    EXPECT_FALSE(book.order_mapping.contains(5001));
    EXPECT_EQ(resting_quantity(book, 5001), std::nullopt);
}

TEST(Book, ExecutePartialReducesQuantity)
{
    Order order(1'000'000'000ull, 5002ull, 'S', 1'500'500u, 200u);
    Book book;
    book.insert(order);

    book.execute_order(5002, 30);

    EXPECT_EQ(resting_quantity(book, 5002), 170u);
    EXPECT_EQ(book.ask_levels.size(), 1u);
}

TEST(Book, ExecuteFullRemovesOrder)
{
    Order order(1'000'000'000ull, 5003ull, 'B', 1'499'900u, 50u);
    Book book;
    book.insert(order);

    book.execute_order(5003, 50);

    EXPECT_EQ(book.bid_levels.size(), 0u);
    EXPECT_FALSE(book.order_mapping.contains(5003));
}

TEST(Book, CancelPartialReducesQuantity)
{
    Order order(1'000'000'000ull, 5004ull, 'S', 1'501'000u, 75u);
    Book book;
    book.insert(order);

    book.cancel_order(5004, 25);

    EXPECT_EQ(resting_quantity(book, 5004), 50u);
}

TEST(Book, ReplaceMovesPriceAndReference)
{
    Order order(1'000'000'000ull, 6004ull, 'S', 1'501'000u, 75u);
    Book book;
    book.insert(order);

    book.replace_order(6004, 6005, 1'500'800u, 60u);

    EXPECT_FALSE(book.order_mapping.contains(6004));
    ASSERT_TRUE(book.order_mapping.contains(6005));
    EXPECT_EQ(resting_quantity(book, 6005), 60u);
    EXPECT_EQ(level_price_for(book, 6005), 1'500'800u);
    EXPECT_FALSE(book.ask_levels.contains(1'501'000u));
    EXPECT_TRUE(book.ask_levels.contains(1'500'800u));
}

TEST(Decoder, SessionMixRebuildsBook)
{
    Decoder decoder;
    auto book = decoder.decode_file(fixture("session_mix.bin"));
    ASSERT_NE(book, nullptr);

    EXPECT_EQ(book->bid_levels.size(), 2u);
    EXPECT_EQ(book->ask_levels.size(), 2u);

    EXPECT_EQ(resting_quantity(*book, 10001), 70u);
    EXPECT_EQ(level_price_for(*book, 10001), 1'500'000u);

    EXPECT_EQ(resting_quantity(*book, 10002), 150u);
    EXPECT_EQ(level_price_for(*book, 10002), 1'500'500u);

    EXPECT_EQ(resting_quantity(*book, 10005), 60u);
    EXPECT_EQ(level_price_for(*book, 10005), 1'500'800u);

    EXPECT_EQ(resting_quantity(*book, 10006), 25u);
    EXPECT_EQ(level_price_for(*book, 10006), 1'499'500u);

    EXPECT_FALSE(book->order_mapping.contains(10003));
    EXPECT_FALSE(book->order_mapping.contains(10004));
    EXPECT_EQ(book->order_mapping.size(), 4u);
}

TEST(Decoder, TinyAddFixture)
{
    Decoder decoder;
    auto book = decoder.decode_file(fixture("add_order_a.bin"));
    ASSERT_NE(book, nullptr);
    EXPECT_EQ(book->bid_levels.size(), 1u);
    EXPECT_EQ(book->ask_levels.size(), 0u);
    EXPECT_EQ(resting_quantity(*book, 12345), 100u);
}
