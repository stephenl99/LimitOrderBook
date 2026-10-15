#include "BookVariants/OrderFlow.h"

#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <unordered_map>

#include "Core/MappedFile.h"
#include "Message/Itch/AddOrderMessage.h"
#include "Message/Itch/DecodeMessage.h"
#include "Message/Itch/FrameReader.h"
#include "Message/Itch/OrderCancelMessage.h"
#include "Message/Itch/OrderDeleteMessage.h"
#include "Message/Itch/OrderExecutedMessage.h"
#include "Message/Itch/OrderExecutedWithPriceMessage.h"
#include "Message/Itch/OrderReplaceMessage.h"

namespace {

struct ShadowOrder {
    Side side;
    Price price;
    std::uint32_t quantity;
    std::size_t live_position;
};

struct ShadowBook {
    std::map<Price, std::deque<std::uint64_t>, std::greater<>> bids;
    std::map<Price, std::deque<std::uint64_t>, std::less<>> asks;
    std::vector<std::uint64_t> live;
    std::unordered_map<std::uint64_t, ShadowOrder> orders;
    std::array<std::uint64_t, 64> recent{};
    std::size_t recent_count = 0;
    Price fair = 0;

    std::optional<Price> best(Side side) const
    {
        if (side == Side::BID) {
            return bids.empty() ? std::nullopt : std::optional<Price>(bids.begin()->first);
        }
        return asks.empty() ? std::nullopt : std::optional<Price>(asks.begin()->first);
    }

    std::deque<std::uint64_t>* best_queue(Side side)
    {
        if (side == Side::BID) {
            return bids.empty() ? nullptr : &bids.begin()->second;
        }
        return asks.empty() ? nullptr : &asks.begin()->second;
    }

    void add(std::uint64_t ref, Side side, Price price, std::uint32_t quantity)
    {
        if (side == Side::BID) {
            bids[price].push_back(ref);
        } else {
            asks[price].push_back(ref);
        }
        orders.emplace(ref, ShadowOrder{side, price, quantity, live.size()});
        live.push_back(ref);
        recent[recent_count++ % recent.size()] = ref;
    }

    void remove(std::uint64_t ref)
    {
        const auto it = orders.find(ref);
        const ShadowOrder order = it->second;
        const auto drop_from = [&](auto& levels) {
            const auto level = levels.find(order.price);
            auto& queue = level->second;
            queue.erase(std::find(queue.begin(), queue.end(), ref));
            if (queue.empty()) {
                levels.erase(level);
            }
        };
        if (order.side == Side::BID) {
            drop_from(bids);
        } else {
            drop_from(asks);
        }
        const std::uint64_t moved = live.back();
        live[order.live_position] = moved;
        orders[moved].live_position = order.live_position;
        live.pop_back();
        orders.erase(it);
    }
};

class FlowGenerator {
public:
    explicit FlowGenerator(const FlowConfig& config)
        : config_(config), rng_(config.seed), books_(config.instruments)
    {
        for (ShadowBook& book : books_) {
            book.fair = config.base_price / config.tick;
        }
    }

    OrderFlow run()
    {
        OrderFlow flow;
        flow.ticks.assign(config_.instruments, config_.tick);
        flow.ops.reserve(config_.instruments * config_.resting_per_instrument + config_.ops);

        for (std::size_t i = 0; i < config_.resting_per_instrument; ++i) {
            for (std::uint32_t instrument = 0; instrument < config_.instruments; ++instrument) {
                add(flow, instrument);
            }
        }
        flow.warmup = flow.ops.size();

        std::uniform_int_distribution<std::uint32_t> pick_instrument(0, config_.instruments - 1);
        while (flow.ops.size() < flow.warmup + config_.ops) {
            step(flow, pick_instrument(rng_));
        }
        flow.ops.resize(flow.warmup + config_.ops);
        flow.peak_resting = peak_resting_;
        return flow;
    }

private:
    void step(OrderFlow& flow, std::uint32_t instrument)
    {
        ShadowBook& book = books_[instrument];
        const double target = static_cast<double>(config_.resting_per_instrument);
        const double live = static_cast<double>(book.live.size());
        if (chance(config_.move_probability)) {
            move_fair_price(flow, instrument);
            return;
        }
        const double add_probability = std::clamp(0.5 + 2.0 * (target - live) / target, 0.05, 0.95);
        if (book.live.empty() || chance(add_probability)) {
            add(flow, instrument);
            return;
        }
        const double kind = unit_(rng_);
        if (kind < 0.70) {
            cancel(flow, instrument);
        } else if (kind < 0.88) {
            execute(flow, instrument);
        } else {
            replace(flow, instrument);
        }
    }

    void add(OrderFlow& flow, std::uint32_t instrument)
    {
        ShadowBook& book = books_[instrument];
        const Side side = chance(0.5) ? Side::BID : Side::ASK;
        const Price price = pick_price(book, side);
        const std::uint32_t quantity = pick_quantity();
        const std::uint64_t ref = next_ref();
        flow.ops.push_back(BookOp{BookOpType::Add, side, instrument, quantity, ref, 0, price});
        book.add(ref, side, price, quantity);
        peak_resting_ = std::max(peak_resting_, ++resting_);
    }

    void cancel(OrderFlow& flow, std::uint32_t instrument)
    {
        ShadowBook& book = books_[instrument];
        const std::uint64_t ref = pick_order(book);
        ShadowOrder& order = book.orders.at(ref);
        if (order.quantity > 1 && chance(0.25)) {
            const std::uint32_t quantity = partial(order.quantity);
            flow.ops.push_back(BookOp{BookOpType::Cancel, order.side, instrument, quantity, ref, 0, 0});
            order.quantity -= quantity;
            return;
        }
        flow.ops.push_back(BookOp{BookOpType::Delete, order.side, instrument, 0, ref, 0, 0});
        book.remove(ref);
        --resting_;
    }

    void execute(OrderFlow& flow, std::uint32_t instrument)
    {
        ShadowBook& book = books_[instrument];
        Side side = chance(0.5) ? Side::BID : Side::ASK;
        if (book.best_queue(side) == nullptr) {
            side = side == Side::BID ? Side::ASK : Side::BID;
        }
        const std::uint64_t ref = book.best_queue(side)->front();
        ShadowOrder& order = book.orders.at(ref);
        const bool fill = order.quantity == 1 || chance(0.5);
        const std::uint32_t quantity = fill ? order.quantity : partial(order.quantity);
        flow.ops.push_back(BookOp{BookOpType::Execute, side, instrument, quantity, ref, 0, 0});
        if (fill) {
            book.remove(ref);
            --resting_;
        } else {
            order.quantity -= quantity;
        }
    }

    void replace(OrderFlow& flow, std::uint32_t instrument)
    {
        ShadowBook& book = books_[instrument];
        const std::uint64_t ref = pick_order(book);
        const Side side = book.orders.at(ref).side;
        book.remove(ref);
        const Price price = pick_price(book, side);
        const std::uint32_t quantity = pick_quantity();
        const std::uint64_t new_ref = next_ref();
        flow.ops.push_back(BookOp{BookOpType::Replace, side, instrument, quantity, ref, new_ref, price});
        book.add(new_ref, side, price, quantity);
    }

    std::uint64_t pick_order(const ShadowBook& book)
    {
        if (book.recent_count != 0 && chance(config_.recent_cancel)) {
            const std::size_t window = std::min(book.recent_count, book.recent.size());
            std::uniform_int_distribution<std::size_t> pick_recent(0, window - 1);
            const std::uint64_t ref = book.recent[pick_recent(rng_)];
            if (book.orders.contains(ref)) {
                return ref;
            }
        }
        std::uniform_int_distribution<std::size_t> pick(0, book.live.size() - 1);
        return book.live[pick(rng_)];
    }

    void move_fair_price(OrderFlow& flow, std::uint32_t instrument)
    {
        ShadowBook& book = books_[instrument];
        const bool up = chance(0.5);
        book.fair += up ? 1 : -1;
        const Side swept = up ? Side::ASK : Side::BID;
        const Price limit = (up ? book.fair : book.fair + 1) * config_.tick;
        std::uint32_t fills = 0;
        while (const auto best = book.best(swept)) {
            if (up ? *best > limit : *best < limit) {
                break;
            }
            const std::uint64_t ref = book.best_queue(swept)->front();
            const BookOpType type = fills++ < kFillsPerMove ? BookOpType::Execute : BookOpType::Delete;
            const std::uint32_t quantity = type == BookOpType::Execute ? book.orders.at(ref).quantity : 0;
            flow.ops.push_back(BookOp{type, swept, instrument, quantity, ref, 0, 0});
            book.remove(ref);
            --resting_;
        }
    }

    Price pick_price(const ShadowBook& book, Side side)
    {
        Price distance = 0;
        if (chance(config_.far_fraction)) {
            std::uniform_int_distribution<std::uint32_t> far(20, std::max<std::uint32_t>(20, config_.far_ticks));
            distance = far(rng_);
        } else {
            distance = std::geometric_distribution<Price>(config_.near_touch)(rng_);
        }
        const Price ticks = side == Side::BID ? book.fair - distance : book.fair + 1 + distance;
        return std::max<Price>(ticks, 1) * config_.tick;
    }

    std::uint32_t pick_quantity()
    {
        std::uniform_int_distribution<std::uint32_t> lots(1, 10);
        return lots(rng_) * 100;
    }

    std::uint32_t partial(std::uint32_t quantity)
    {
        std::uniform_int_distribution<std::uint32_t> amount(1, quantity - 1);
        return amount(rng_);
    }

    std::uint64_t next_ref()
    {
        const std::uint64_t sequence = ++ref_counter_;
        if (!config_.random_ids) {
            return sequence;
        }
        std::uint64_t z = sequence * 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return (z ^ (z >> 31)) >> 1;
    }

    bool chance(double probability) { return unit_(rng_) < probability; }

    static constexpr std::uint32_t kFillsPerMove = 2;

    FlowConfig config_;
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> unit_{0.0, 1.0};
    std::vector<ShadowBook> books_;
    std::uint64_t ref_counter_ = 0;
    std::size_t resting_ = 0;
    std::size_t peak_resting_ = 0;
};

struct LiveOrder {
    std::uint32_t instrument;
    std::uint32_t quantity;
    Side side;
};

}  // namespace

OrderFlow generate_order_flow(const FlowConfig& config)
{
    return FlowGenerator(config).run();
}

std::expected<OrderFlow, std::string> load_itch_order_flow(const std::filesystem::path& path)
{
    const auto file = MappedFile::open(path);
    if (!file) {
        return std::unexpected(file.error());
    }

    OrderFlow flow;
    std::unordered_map<std::uint16_t, std::uint32_t> instruments;
    std::unordered_map<std::uint64_t, LiveOrder> live;

    const auto instrument_for = [&](std::uint16_t locate) {
        const auto [it, inserted] = instruments.try_emplace(locate, static_cast<std::uint32_t>(instruments.size()));
        if (inserted) {
            flow.ticks.push_back(0);
        }
        return it->second;
    };
    const auto note_price = [&](std::uint32_t instrument, Price price) {
        flow.ticks[instrument] = std::gcd(flow.ticks[instrument], price);
    };
    const auto reduce = [&](BookOpType type, std::uint64_t ref, std::uint32_t quantity) {
        const auto it = live.find(ref);
        if (it == live.end()) {
            return;
        }
        LiveOrder& order = it->second;
        if (quantity >= order.quantity) {
            flow.ops.push_back(BookOp{type, order.side, order.instrument, order.quantity, ref, 0, 0});
            live.erase(it);
            return;
        }
        flow.ops.push_back(BookOp{type, order.side, order.instrument, quantity, ref, 0, 0});
        order.quantity -= quantity;
    };

    itch::FrameReader frames(file->bytes());
    while (const auto frame = frames.next()) {
        const auto message = itch::decode_message(*frame);
        if (message == nullptr) {
            continue;
        }
        switch (message->message_type()) {
        case 'A':
        case 'F': {
            const auto& add = dynamic_cast<const itch::AddOrderMessage&>(*message);
            const std::uint32_t instrument = instrument_for(message->stock_locate());
            const Side side = add.side() == 'B' ? Side::BID : Side::ASK;
            const auto ref = add.order_reference_number();
            if (add.shares() == 0 || !live.try_emplace(ref, LiveOrder{instrument, add.shares(), side}).second) {
                break;
            }
            note_price(instrument, add.price());
            flow.ops.push_back(BookOp{BookOpType::Add, side, instrument, add.shares(), ref, 0, add.price()});
            break;
        }
        case 'E': {
            const auto& executed = dynamic_cast<const itch::OrderExecutedMessage&>(*message);
            reduce(BookOpType::Execute, executed.order_reference_number(), executed.executed_shares());
            break;
        }
        case 'C': {
            const auto& executed = dynamic_cast<const itch::OrderExecutedWithPriceMessage&>(*message);
            reduce(BookOpType::Execute, executed.order_reference_number(), executed.executed_shares());
            break;
        }
        case 'X': {
            const auto& cancel = dynamic_cast<const itch::OrderCancelMessage&>(*message);
            reduce(BookOpType::Cancel, cancel.order_reference_number(), cancel.cancelled_shares());
            break;
        }
        case 'D': {
            const auto& removed = dynamic_cast<const itch::OrderDeleteMessage&>(*message);
            const auto it = live.find(removed.order_reference_number());
            if (it != live.end()) {
                flow.ops.push_back(BookOp{BookOpType::Delete, it->second.side, it->second.instrument, 0, it->first, 0, 0});
                live.erase(it);
            }
            break;
        }
        case 'U': {
            const auto& replaced = dynamic_cast<const itch::OrderReplaceMessage&>(*message);
            const auto it = live.find(replaced.original_order_reference());
            if (it == live.end() || replaced.shares() == 0 || live.contains(replaced.new_order_reference())) {
                break;
            }
            const LiveOrder order = it->second;
            live.erase(it);
            live.emplace(replaced.new_order_reference(), LiveOrder{order.instrument, replaced.shares(), order.side});
            note_price(order.instrument, replaced.price());
            flow.ops.push_back(BookOp{BookOpType::Replace, order.side, order.instrument, replaced.shares(),
                                      replaced.original_order_reference(), replaced.new_order_reference(),
                                      replaced.price()});
            break;
        }
        default:
            break;
        }
        flow.peak_resting = std::max(flow.peak_resting, live.size());
    }

    for (Price& tick : flow.ticks) {
        tick = std::max<Price>(tick, 1);
    }
    return flow;
}
