#include "BookVariants/BookVariants.h"
#include "BookVariants/OrderFlow.h"
#include "Book/Book.h"
#include "Baseline/SingleThreadDecoder.h"
#include "Core/Decoder.h"
#include "Core/ShardedDecoder.h"
#include "Book/StockDirectory.h"
#include "Message/Itch/AddOrderMessage.h"
#include "Message/Itch/DecodeMessage.h"
#include "Message/Itch/OrderCancelMessage.h"
#include "Message/Itch/OrderDeleteMessage.h"
#include "Message/Itch/OrderExecutedWithPriceMessage.h"
#include "Message/Itch/OrderReplaceMessage.h"
#include "Message/Itch/StockDirectoryMessage.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <tuple>
#include <unordered_map>
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

std::optional<uint32_t> resting_quantity(const SecurityBook& book, uint64_t order_reference_number)
{
    const auto it = book.order_mapping.find(order_reference_number);
    if (it == book.order_mapping.end()) {
        return std::nullopt;
    }
    return it->second.it->quantity();
}

std::optional<uint32_t> level_price_for(const SecurityBook& book, uint64_t order_reference_number)
{
    const auto it = book.order_mapping.find(order_reference_number);
    if (it == book.order_mapping.end()) {
        return std::nullopt;
    }
    return it->second.price;
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

std::optional<SecurityBook*> find_security_book(const Book& books, uint16_t stock_locate = 1)
{
    return books.find(stock_locate);
}

void insert_order(SecurityBook& book, Order order)
{
    book.insert(order);
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
    EXPECT_EQ(msg->stock_locate(), 1u);
}

TEST(DecodeMessage, StockDirectoryPayload)
{
    std::vector<uint8_t> data(39, ' ');
    data[0] = 'R';
    data[1] = 0;
    data[2] = 42;  // locate 42
    const char symbol[] = "MSFT    ";
    for (int i = 0; i < 8; ++i) {
        data[11 + i] = static_cast<uint8_t>(symbol[i]);
    }
    data[19] = 'Q';

    auto msg = itch::decode_message(data);
    ASSERT_NE(msg, nullptr);
    const auto& directory = static_cast<const itch::StockDirectoryMessage&>(*msg);
    EXPECT_EQ(directory.stock_locate(), 42u);
    EXPECT_EQ(std::string(directory.stock(), 8), "MSFT    ");
    EXPECT_EQ(directory.market_category(), 'Q');
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
    Order order(1'000'000'000ull, 1u, 12345ull, 'B', 1'500'000u, 100u);
    SecurityBook book;
    insert_order(book, order);
    EXPECT_EQ(book.bid_levels.size(), 1u);
    EXPECT_EQ(book.ask_levels.size(), 0u);
    ASSERT_FALSE(book.bid_levels.begin()->second.orders.empty());
    EXPECT_EQ(book.bid_levels.begin()->second.orders.front().order_reference_number(), 12345u);
    EXPECT_EQ(book.bid_levels.begin()->second.orders.front().stock_locate(), 1u);
    EXPECT_EQ(resting_quantity(book, 12345), 100u);
}

TEST(Book, DeleteOrderRemovesRestingOrder)
{
    Order order(1'000'000'000ull, 1u, 5001ull, 'B', 1'500'000u, 100u);
    SecurityBook book;
    insert_order(book, order);
    ASSERT_TRUE(book.order_mapping.contains(5001));

    book.delete_order(5001);

    EXPECT_EQ(book.bid_levels.size(), 0u);
    EXPECT_FALSE(book.order_mapping.contains(5001));
    EXPECT_EQ(resting_quantity(book, 5001), std::nullopt);
}

TEST(Book, ExecutePartialReducesQuantity)
{
    Order order(1'000'000'000ull, 1u, 5002ull, 'S', 1'500'500u, 200u);
    SecurityBook book;
    insert_order(book, order);

    book.execute_order(5002, 30);

    EXPECT_EQ(resting_quantity(book, 5002), 170u);
    EXPECT_EQ(book.ask_levels.size(), 1u);
}

TEST(Book, ExecuteFullRemovesOrder)
{
    Order order(1'000'000'000ull, 1u, 5003ull, 'B', 1'499'900u, 50u);
    SecurityBook book;
    insert_order(book, order);

    book.execute_order(5003, 50);

    EXPECT_EQ(book.bid_levels.size(), 0u);
    EXPECT_FALSE(book.order_mapping.contains(5003));
}

TEST(Book, CancelPartialReducesQuantity)
{
    Order order(1'000'000'000ull, 1u, 5004ull, 'S', 1'501'000u, 75u);
    SecurityBook book;
    insert_order(book, order);

    book.cancel_order(5004, 25);

    EXPECT_EQ(resting_quantity(book, 5004), 50u);
}

TEST(Book, ReplaceMovesPriceAndReference)
{
    Order order(1'000'000'000ull, 1u, 6004ull, 'S', 1'501'000u, 75u);
    SecurityBook book;
    insert_order(book, order);

    book.replace_order(6004, 6005, 1'500'800u, 60u);

    EXPECT_FALSE(book.order_mapping.contains(6004));
    ASSERT_TRUE(book.order_mapping.contains(6005));
    EXPECT_EQ(resting_quantity(book, 6005), 60u);
    EXPECT_EQ(level_price_for(book, 6005), 1'500'800u);
    EXPECT_FALSE(book.ask_levels.contains(1'501'000u));
    EXPECT_TRUE(book.ask_levels.contains(1'500'800u));
}

TEST(StockDirectory, AddAndLookup)
{
    StockDirectory directory;
    directory.add(42, "MSFT    ");
    const auto symbol = directory.lookup(42);
    ASSERT_TRUE(symbol.has_value());
    EXPECT_EQ(*symbol, "MSFT");
    EXPECT_EQ(directory.translate(42), "MSFT");
    EXPECT_TRUE(directory.lookup(99).has_value() == false);
}

TEST(Decoder, SessionMixRebuildsBook)
{
    auto result = Decoder::decode_file(fixture("session_mix.bin"));
    ASSERT_TRUE(result.has_value()) << result.error();
    const std::unique_ptr<Book>& books = *result;
    const auto book = find_security_book(*books, 1);
    ASSERT_TRUE(book.has_value());
    EXPECT_EQ((*book)->stock_locate(), 1u);

    EXPECT_EQ((*book)->bid_levels.size(), 2u);
    EXPECT_EQ((*book)->ask_levels.size(), 2u);

    EXPECT_EQ(resting_quantity(**book, 10001), 70u);
    EXPECT_EQ(level_price_for(**book, 10001), 1'500'000u);

    EXPECT_EQ(resting_quantity(**book, 10002), 150u);
    EXPECT_EQ(level_price_for(**book, 10002), 1'500'500u);

    EXPECT_EQ(resting_quantity(**book, 10005), 60u);
    EXPECT_EQ(level_price_for(**book, 10005), 1'500'800u);

    EXPECT_EQ(resting_quantity(**book, 10006), 25u);
    EXPECT_EQ(level_price_for(**book, 10006), 1'499'500u);

    EXPECT_FALSE((*book)->order_mapping.contains(10003));
    EXPECT_FALSE((*book)->order_mapping.contains(10004));
    EXPECT_EQ((*book)->order_mapping.size(), 4u);
}

TEST(Decoder, TinyAddFixture)
{
    auto result = Decoder::decode_file(fixture("add_order_a.bin"));
    ASSERT_TRUE(result.has_value()) << result.error();
    const std::unique_ptr<Book>& books = *result;
    const auto book = find_security_book(*books, 1);
    ASSERT_TRUE(book.has_value());
    EXPECT_EQ((*book)->stock_locate(), 1u);
    EXPECT_EQ((*book)->bid_levels.size(), 1u);
    EXPECT_EQ((*book)->ask_levels.size(), 0u);
    EXPECT_EQ(resting_quantity(**book, 12345), 100u);
}

TEST(ShardedDecoder, MatchesSingleThreadOnEveryShardCount)
{
    const auto path = fixture("session_mix.bin");
    auto expected = SingleThreadDecoder::decode_file(path);
    ASSERT_TRUE(expected.has_value()) << expected.error();
    for (std::size_t shards : {1u, 2u, 3u, 8u}) {
        auto result = ShardedDecoder::decode_file(path, shards);
        ASSERT_TRUE(result.has_value()) << result.error();
        EXPECT_EQ((*result)->security_locates().size(), (*expected)->security_locates().size()) << shards;
        for (const InstrumentId locate : (*expected)->security_locates()) {
            const auto want = (*expected)->find(locate);
            const auto got = (*result)->find(locate);
            ASSERT_TRUE(got.has_value()) << "locate " << locate << " shards " << shards;
            EXPECT_EQ((*got)->order_mapping.size(), (*want)->order_mapping.size());
            EXPECT_EQ(depth_snapshot((*got)->bid_levels, 100).size(), depth_snapshot((*want)->bid_levels, 100).size());
            EXPECT_EQ(depth_snapshot((*got)->ask_levels, 100).size(), depth_snapshot((*want)->ask_levels, 100).size());
        }
        EXPECT_EQ((*result)->directory.lookup(1), (*expected)->directory.lookup(1)) << shards;
    }
}

namespace {

using LevelQueues = std::vector<std::vector<uint64_t>>;

template <typename Levels>
LevelQueues queue_by_level(const Levels& levels)
{
    LevelQueues queues;
    for (const auto& entry : levels) {
        std::vector<uint64_t> ids;
        ids.push_back(static_cast<uint64_t>(entry.first));
        for (const Order& order : entry.second.orders) {
            ids.push_back(order.order_reference_number());
            ids.push_back(order.quantity());
        }
        queues.push_back(std::move(ids));
    }
    return queues;
}

template <typename Levels>
LevelQueues book_queues(const BasicSecurityBook<Levels>& book, Side side)
{
    return side == Side::BID ? queue_by_level(book.bid_levels) : queue_by_level(book.ask_levels);
}

template <typename Levels, typename Orders>
LevelQueues book_queues(const AggregateSecurityBook<Levels, Orders>& book, Side side)
{
    std::map<Price, std::vector<std::tuple<uint64_t, uint64_t, uint32_t>>> by_price;
    book.for_each_order([&](uint64_t id, const RestingOrder& order) {
        if (order.side == side) {
            by_price[order.price].emplace_back(order.sequence, id, order.quantity);
        }
    });
    LevelQueues queues;
    for (auto& [price, orders] : by_price) {
        std::sort(orders.begin(), orders.end());
        std::vector<uint64_t> ids{static_cast<uint64_t>(price)};
        for (const auto& [sequence, id, quantity] : orders) {
            ids.push_back(id);
            ids.push_back(quantity);
        }
        queues.push_back(std::move(ids));
    }
    if (side == Side::BID) {
        std::reverse(queues.begin(), queues.end());
    }
    return queues;
}

std::vector<std::pair<Price, uint32_t>> as_pairs(const std::vector<LevelSummary>& levels)
{
    std::vector<std::pair<Price, uint32_t>> pairs;
    for (const LevelSummary& level : levels) {
        pairs.emplace_back(level.price, level.total_shares);
    }
    return pairs;
}

}  // namespace

template <typename CandidateBook>
void expect_matches_tree_book(int compare_queues_every)
{
    constexpr Price kTick = 25;
    constexpr Price kBestBid = 1'000'000;
    constexpr Price kBestAsk = kBestBid + kTick;
    SecurityBook tree(1);
    CandidateBook candidate(1);
    std::mt19937_64 random(20261014);
    std::vector<uint64_t> live;
    uint64_t next_id = 1;

    for (int step = 0; step < 5000; ++step) {
        const auto roll = random() % 10;
        if (step == 2500) {
            tree.clear();
            candidate.clear();
            live.clear();
        } else if (live.empty() || roll < 4) {
            const char side = random() % 2 == 0 ? 'B' : 'S';
            const Price ticks = static_cast<Price>(random() % 60);
            const Price price = side == 'B' ? kBestBid - ticks * kTick : kBestAsk + ticks * kTick;
            const auto quantity = static_cast<uint32_t>(1 + random() % 500);
            const Order order(step, 1, next_id, side, price, quantity);
            tree.insert(order);
            candidate.insert(order);
            live.push_back(next_id++);
        } else {
            const std::size_t pick = random() % live.size();
            const uint64_t id = live[pick];
            const Order& resting = *tree.order_mapping.at(id).it;
            const Price price = resting.price();
            const uint32_t quantity = resting.quantity();
            if (roll < 6) {
                const auto cancelled = static_cast<uint32_t>(1 + random() % quantity);
                tree.cancel_order(id, cancelled);
                candidate.cancel_order(id, cancelled);
            } else if (roll < 9) {
                const Price moved = price + (static_cast<Price>(random() % 5) - 2) * kTick;
                const Price new_price = resting.side() == Side::BID ? std::min(moved, kBestBid) : std::max(moved, kBestAsk);
                const auto new_quantity = static_cast<uint32_t>(random() % 600);
                tree.modify_order(id, new_price, new_quantity);
                candidate.modify_order(id, new_price, new_quantity);
            } else {
                tree.delete_order(id);
                candidate.delete_order(id);
            }
            if (!tree.order_mapping.contains(id)) {
                live[pick] = live.back();
                live.pop_back();
            }
        }
        ASSERT_EQ(candidate.resting_orders(), tree.resting_orders()) << "step " << step;
        ASSERT_EQ(candidate.bid_level_count(), tree.bid_level_count()) << "step " << step;
        ASSERT_EQ(candidate.ask_level_count(), tree.ask_level_count()) << "step " << step;
        ASSERT_EQ(as_pairs(candidate.bid_depth(1000)), as_pairs(tree.bid_depth(1000))) << "step " << step;
        ASSERT_EQ(as_pairs(candidate.ask_depth(1000)), as_pairs(tree.ask_depth(1000))) << "step " << step;
        if (step % compare_queues_every == 0) {
            ASSERT_EQ(book_queues(candidate, Side::BID), book_queues(tree, Side::BID)) << "step " << step;
            ASSERT_EQ(book_queues(candidate, Side::ASK), book_queues(tree, Side::ASK)) << "step " << step;
        }
    }
}

TEST(FlatBook, LinearMatchesTreeBookOnRandomOperations)
{
    expect_matches_tree_book<FlatSecurityBook>(1);
}

TEST(FlatBook, HybridMatchesTreeBookOnRandomOperations)
{
    expect_matches_tree_book<HybridFlatSecurityBook>(1);
}

TEST(AggregateBook, SortedLevelsMatchTreeBookOnRandomOperations)
{
    expect_matches_tree_book<SortedAggregateSecurityBook>(10);
}

TEST(AggregateBook, FlatIndexMatchesTreeBookOnRandomOperations)
{
    expect_matches_tree_book<SortedAggregateFlatIndexSecurityBook>(10);
}

TEST(AggregateBook, LadderMatchesTreeBookOnRandomOperations)
{
    expect_matches_tree_book<LadderSecurityBook>(10);
}

TEST(AggregateBook, IdentityHashLadderMatchesTreeBookOnRandomOperations)
{
    expect_matches_tree_book<IdentityLadderSecurityBook>(10);
}

template <typename Index>
void expect_index_matches_unordered_map()
{
    Index index;
    std::unordered_map<uint64_t, uint32_t> reference;
    std::mt19937_64 random(7);
    std::vector<uint64_t> keys;
    for (int step = 0; step < 200000; ++step) {
        if (keys.empty() || random() % 3 != 0) {
            const uint64_t key = random() % 50000;
            const auto quantity = static_cast<uint32_t>(random());
            const bool inserted = index.insert(key, RestingOrder{0, 0, quantity, Side::BID});
            ASSERT_EQ(inserted, reference.emplace(key, quantity).second) << step;
            if (inserted) {
                keys.push_back(key);
            }
        } else {
            const std::size_t pick = random() % keys.size();
            const uint64_t key = keys[pick];
            const auto handle = index.find(key);
            ASSERT_TRUE(handle) << step;
            index.erase(handle);
            reference.erase(key);
            keys[pick] = keys.back();
            keys.pop_back();
        }
        ASSERT_EQ(index.size(), reference.size()) << step;
    }
    for (const auto& [key, quantity] : reference) {
        const auto handle = index.find(key);
        ASSERT_TRUE(handle);
        EXPECT_EQ(index.get(handle).quantity, quantity);
    }
    std::size_t visited = 0;
    index.for_each([&](uint64_t, const RestingOrder&) { ++visited; });
    EXPECT_EQ(visited, reference.size());
}

TEST(FlatOrderIndex, MultiplicativeHashMatchesUnorderedMapUnderHeavyChurn)
{
    expect_index_matches_unordered_map<FlatOrderIndex>();
}

TEST(FlatOrderIndex, IdentityHashMatchesUnorderedMapUnderHeavyChurn)
{
    expect_index_matches_unordered_map<IdentityFlatOrderIndex>();
}

TEST(PriceLadder, RefinesTickAndGrowsBothWays)
{
    PriceLadder<std::greater<>> bids;
    PriceLadder<std::less<>> asks;
    std::map<Price, uint32_t, std::greater<>> bid_reference;
    std::map<Price, uint32_t> ask_reference;
    const std::vector<Price> prices = {1000, 1300, 1150, 700, 1025, 1975, 25, 1000, 1300};
    for (const Price price : prices) {
        bids.add(price, 10);
        asks.add(price, 10);
        bid_reference[price] += 10;
        ask_reference[price] += 10;
    }
    bids.remove(1975, 10);
    asks.remove(25, 10);
    bid_reference.erase(1975);
    ask_reference.erase(25);
    bids.adjust(1000, 5);
    bid_reference[1000] += 5;

    std::vector<std::pair<Price, uint32_t>> bid_expected(bid_reference.begin(), bid_reference.end());
    std::vector<std::pair<Price, uint32_t>> ask_expected(ask_reference.begin(), ask_reference.end());
    EXPECT_EQ(as_pairs(bids.depth(100)), bid_expected);
    EXPECT_EQ(as_pairs(asks.depth(100)), ask_expected);
    EXPECT_EQ(bids.size(), bid_expected.size());
    EXPECT_EQ(asks.size(), ask_expected.size());
}

namespace {

template <typename S>
void apply_flow_op(S& book, const BookOp& op)
{
    switch (op.type) {
    case BookOpType::Add:
        book.add_order(op.ref, op.side, op.price, op.quantity);
        break;
    case BookOpType::Execute:
        book.execute_order(op.ref, op.quantity);
        break;
    case BookOpType::Cancel:
        book.cancel_order(op.ref, op.quantity);
        break;
    case BookOpType::Delete:
        book.delete_order(op.ref);
        break;
    case BookOpType::Replace:
        book.replace_order(op.ref, op.new_ref, op.price, op.quantity);
        break;
    }
}

}  // namespace

TEST(BookVariants, EveryVariantMatchesTreeOnGeneratedFlow)
{
    for (const bool random_ids : {false, true}) {
        FlowConfig config;
        config.ops = 60'000;
        config.instruments = 3;
        config.resting_per_instrument = 300;
        config.far_fraction = 0.05;
        config.far_ticks = 3000;
        config.move_probability = 0.05;
        config.random_ids = random_ids;
        const OrderFlow flow = generate_order_flow(config);

        for_each_book_variant([&]<typename B>(std::type_identity<B>, const BookVariantInfo& info) {
            Book expected;
            B candidate;
            for (std::size_t i = 0; i < flow.ops.size(); ++i) {
                const BookOp& op = flow.ops[i];
                auto& want = expected.book_for(op.instrument);
                auto& got = candidate.book_for(op.instrument);
                apply_flow_op(want, op);
                apply_flow_op(got, op);
                ASSERT_EQ(got.best_bid(), want.best_bid()) << info.name << " op " << i;
                ASSERT_EQ(got.best_ask(), want.best_ask()) << info.name << " op " << i;
                if (i % 5'000 != 0) {
                    continue;
                }
                for (InstrumentId instrument = 0; instrument < config.instruments; ++instrument) {
                    const auto& a = candidate.book_for(instrument);
                    const auto& b = expected.book_for(instrument);
                    ASSERT_EQ(a.resting_orders(), b.resting_orders()) << info.name << " op " << i;
                    ASSERT_EQ(as_pairs(a.bid_depth(SIZE_MAX)), as_pairs(b.bid_depth(SIZE_MAX))) << info.name << " op " << i;
                    ASSERT_EQ(as_pairs(a.ask_depth(SIZE_MAX)), as_pairs(b.ask_depth(SIZE_MAX))) << info.name << " op " << i;
                }
            }
        });
    }
}

TEST(BookVariants, PooledSlotIndexMatchesUnorderedMap)
{
    pooled::SlotIndex index;
    std::unordered_map<std::uint64_t, std::uint32_t> expected;
    std::mt19937_64 rng(7);
    std::uniform_int_distribution<std::uint64_t> key(0, 4'000);
    for (std::uint32_t i = 0; i < 400'000; ++i) {
        const std::uint64_t ref = key(rng) * 1024;
        if (rng() % 3 == 0) {
            index.erase(ref);
            expected.erase(ref);
        } else {
            index.insert(ref, i);
            expected[ref] = i;
        }
        ASSERT_EQ(index.size(), expected.size());
        const std::uint64_t probe = key(rng) * 1024;
        const auto it = expected.find(probe);
        ASSERT_EQ(index.find(probe), it == expected.end() ? pooled::kNoSlot : it->second);
    }
}
