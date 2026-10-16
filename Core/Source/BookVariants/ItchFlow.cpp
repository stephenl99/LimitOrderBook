#include <algorithm>
#include <numeric>
#include <unordered_map>

#include "BookVariants/OrderFlow.h"
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

struct LiveOrder {
    std::uint32_t instrument;
    std::uint32_t quantity;
    Side side;
};

class ItchFlowBuilder {
public:
    void on_message(const itch::Message& message)
    {
        switch (message.message_type()) {
        case 'A':
        case 'F':
            add(dynamic_cast<const itch::AddOrderMessage&>(message));
            break;
        case 'E': {
            const auto& executed = dynamic_cast<const itch::OrderExecutedMessage&>(message);
            reduce(BookOpType::Execute, executed.order_reference_number(), executed.executed_shares());
            break;
        }
        case 'C': {
            const auto& executed = dynamic_cast<const itch::OrderExecutedWithPriceMessage&>(message);
            reduce(BookOpType::Execute, executed.order_reference_number(), executed.executed_shares());
            break;
        }
        case 'X': {
            const auto& cancel = dynamic_cast<const itch::OrderCancelMessage&>(message);
            reduce(BookOpType::Cancel, cancel.order_reference_number(), cancel.cancelled_shares());
            break;
        }
        case 'D':
            remove(dynamic_cast<const itch::OrderDeleteMessage&>(message).order_reference_number());
            break;
        case 'U':
            replace(dynamic_cast<const itch::OrderReplaceMessage&>(message));
            break;
        default:
            break;
        }
        flow_.peak_resting = std::max(flow_.peak_resting, live_.size());
    }

    OrderFlow finish()
    {
        for (Price& tick : flow_.ticks) {
            tick = std::max<Price>(tick, 1);
        }
        return std::move(flow_);
    }

private:
    void add(const itch::AddOrderMessage& add)
    {
        const std::uint32_t instrument = instrument_for(add.stock_locate());
        const Side side = add.side() == 'B' ? Side::BID : Side::ASK;
        const std::uint64_t ref = add.order_reference_number();
        if (add.shares() == 0 || !live_.try_emplace(ref, LiveOrder{instrument, add.shares(), side}).second) {
            return;
        }
        note_price(instrument, add.price());
        flow_.ops.push_back({.type = BookOpType::Add, .side = side, .instrument = instrument, .quantity = add.shares(),
                             .ref = ref, .price = add.price()});
    }

    void reduce(BookOpType type, std::uint64_t ref, std::uint32_t quantity)
    {
        const auto found = live_.find(ref);
        if (found == live_.end()) {
            return;
        }
        LiveOrder& order = found->second;
        const std::uint32_t removed = std::min(quantity, order.quantity);
        flow_.ops.push_back({.type = type, .side = order.side, .instrument = order.instrument, .quantity = removed, .ref = ref});
        if (removed == order.quantity) {
            live_.erase(found);
        } else {
            order.quantity -= removed;
        }
    }

    void remove(std::uint64_t ref)
    {
        const auto found = live_.find(ref);
        if (found == live_.end()) {
            return;
        }
        flow_.ops.push_back({.type = BookOpType::Delete, .side = found->second.side, .instrument = found->second.instrument,
                             .ref = ref});
        live_.erase(found);
    }

    void replace(const itch::OrderReplaceMessage& replaced)
    {
        const auto found = live_.find(replaced.original_order_reference());
        if (found == live_.end() || replaced.shares() == 0 || live_.contains(replaced.new_order_reference())) {
            return;
        }
        const LiveOrder order = found->second;
        live_.erase(found);
        live_.emplace(replaced.new_order_reference(), LiveOrder{order.instrument, replaced.shares(), order.side});
        note_price(order.instrument, replaced.price());
        flow_.ops.push_back({.type = BookOpType::Replace, .side = order.side, .instrument = order.instrument,
                             .quantity = replaced.shares(), .ref = replaced.original_order_reference(),
                             .new_ref = replaced.new_order_reference(), .price = replaced.price()});
    }

    std::uint32_t instrument_for(std::uint16_t locate)
    {
        const auto [entry, inserted] = instruments_.try_emplace(locate, static_cast<std::uint32_t>(instruments_.size()));
        if (inserted) {
            flow_.ticks.push_back(0);
        }
        return entry->second;
    }

    void note_price(std::uint32_t instrument, Price price) { flow_.ticks[instrument] = std::gcd(flow_.ticks[instrument], price); }

    OrderFlow flow_;
    std::unordered_map<std::uint16_t, std::uint32_t> instruments_;
    std::unordered_map<std::uint64_t, LiveOrder> live_;
};

}  // namespace

std::expected<OrderFlow, std::string> load_itch_order_flow(const std::filesystem::path& path)
{
    const auto file = MappedFile::open(path);
    if (!file) {
        return std::unexpected(file.error());
    }
    ItchFlowBuilder builder;
    itch::FrameReader frames(file->bytes());
    while (const auto frame = frames.next()) {
        if (const auto message = itch::decode_message(*frame)) {
            builder.on_message(*message);
        }
    }
    return builder.finish();
}
