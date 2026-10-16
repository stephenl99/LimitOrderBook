#include <algorithm>
#include <random>
#include <vector>

#include "BookVariants/OrderFlow.h"
#include "BookVariants/ShadowBook.h"

namespace {

class FlowGenerator {
public:
    explicit FlowGenerator(const FlowConfig& config) : config_(config), rng_(config.seed), books_(config.instruments)
    {
        for (ShadowBook& book : books_) {
            book.fair = config.base_price / config.tick;
        }
    }

    OrderFlow run()
    {
        flow_.ticks.assign(config_.instruments, config_.tick);
        flow_.ops.reserve(config_.instruments * config_.resting_per_instrument + config_.ops);

        for (std::size_t i = 0; i < config_.resting_per_instrument; ++i) {
            for (std::uint32_t instrument = 0; instrument < config_.instruments; ++instrument) {
                add(instrument);
            }
        }
        flow_.warmup = flow_.ops.size();

        std::uniform_int_distribution<std::uint32_t> pick_instrument(0, config_.instruments - 1);
        while (flow_.ops.size() < flow_.warmup + config_.ops) {
            step(pick_instrument(rng_));
        }
        flow_.ops.resize(flow_.warmup + config_.ops);
        flow_.peak_resting = peak_resting_;
        return std::move(flow_);
    }

private:
    static constexpr std::uint32_t kFillsPerMove = 2;

    void step(std::uint32_t instrument)
    {
        if (chance(config_.move_probability)) {
            move_fair_price(instrument);
            return;
        }
        const ShadowBook& book = books_[instrument];
        const auto target = static_cast<double>(config_.resting_per_instrument);
        const auto resting = static_cast<double>(book.size());
        const double add_probability = std::clamp(0.5 + 2.0 * (target - resting) / target, 0.05, 0.95);
        if (book.empty() || chance(add_probability)) {
            add(instrument);
            return;
        }
        const double kind = unit_(rng_);
        if (kind < 0.70) {
            cancel(instrument);
        } else if (kind < 0.88) {
            execute(instrument);
        } else {
            replace(instrument);
        }
    }

    void add(std::uint32_t instrument)
    {
        ShadowBook& book = books_[instrument];
        const Side side = chance(0.5) ? Side::BID : Side::ASK;
        const Price price = pick_price(book, side);
        const std::uint32_t quantity = pick_quantity();
        const std::uint64_t ref = next_ref();
        emit({.type = BookOpType::Add, .side = side, .instrument = instrument, .quantity = quantity, .ref = ref, .price = price});
        book.add(ref, side, price, quantity);
        peak_resting_ = std::max(peak_resting_, ++resting_);
    }

    void cancel(std::uint32_t instrument)
    {
        ShadowBook& book = books_[instrument];
        const std::uint64_t ref = pick_order(book);
        ShadowOrder& order = book.order(ref);
        if (order.quantity > 1 && chance(0.25)) {
            const std::uint32_t quantity = partial(order.quantity);
            emit({.type = BookOpType::Cancel, .side = order.side, .instrument = instrument, .quantity = quantity, .ref = ref});
            order.quantity -= quantity;
            return;
        }
        emit({.type = BookOpType::Delete, .side = order.side, .instrument = instrument, .ref = ref});
        remove(book, ref);
    }

    void execute(std::uint32_t instrument)
    {
        ShadowBook& book = books_[instrument];
        Side side = chance(0.5) ? Side::BID : Side::ASK;
        if (book.best_queue(side) == nullptr) {
            side = side == Side::BID ? Side::ASK : Side::BID;
        }
        const std::uint64_t ref = book.best_queue(side)->front();
        ShadowOrder& order = book.order(ref);
        const bool fills_completely = order.quantity == 1 || chance(0.5);
        const std::uint32_t quantity = fills_completely ? order.quantity : partial(order.quantity);
        emit({.type = BookOpType::Execute, .side = side, .instrument = instrument, .quantity = quantity, .ref = ref});
        if (fills_completely) {
            remove(book, ref);
        } else {
            order.quantity -= quantity;
        }
    }

    void replace(std::uint32_t instrument)
    {
        ShadowBook& book = books_[instrument];
        const std::uint64_t ref = pick_order(book);
        const Side side = book.order(ref).side;
        book.remove(ref);
        const Price price = pick_price(book, side);
        const std::uint32_t quantity = pick_quantity();
        const std::uint64_t new_ref = next_ref();
        emit({.type = BookOpType::Replace, .side = side, .instrument = instrument, .quantity = quantity, .ref = ref,
              .new_ref = new_ref, .price = price});
        book.add(new_ref, side, price, quantity);
    }

    void move_fair_price(std::uint32_t instrument)
    {
        ShadowBook& book = books_[instrument];
        if (chance(0.5)) {
            ++book.fair;
            sweep(instrument, Side::ASK, [&](Price price) { return price <= book.fair * config_.tick; });
        } else {
            --book.fair;
            sweep(instrument, Side::BID, [&](Price price) { return price >= (book.fair + 1) * config_.tick; });
        }
    }

    template <typename Crossed>
    void sweep(std::uint32_t instrument, Side side, Crossed crossed)
    {
        ShadowBook& book = books_[instrument];
        std::uint32_t fills = 0;
        while (const auto best = book.best_price(side)) {
            if (!crossed(*best)) {
                break;
            }
            const std::uint64_t ref = book.best_queue(side)->front();
            if (fills++ < kFillsPerMove) {
                emit({.type = BookOpType::Execute, .side = side, .instrument = instrument,
                      .quantity = book.order(ref).quantity, .ref = ref});
            } else {
                emit({.type = BookOpType::Delete, .side = side, .instrument = instrument, .ref = ref});
            }
            remove(book, ref);
        }
    }

    std::uint64_t pick_order(const ShadowBook& book)
    {
        if (book.recent_window() != 0 && chance(config_.recent_cancel)) {
            std::uniform_int_distribution<std::size_t> pick_recent(0, book.recent_window() - 1);
            const std::uint64_t ref = book.recent_order(pick_recent(rng_));
            if (book.contains(ref)) {
                return ref;
            }
        }
        std::uniform_int_distribution<std::size_t> pick(0, book.size() - 1);
        return book.live_order(pick(rng_));
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

    void remove(ShadowBook& book, std::uint64_t ref)
    {
        book.remove(ref);
        --resting_;
    }

    void emit(const BookOp& op) { flow_.ops.push_back(op); }

    bool chance(double probability) { return unit_(rng_) < probability; }

    FlowConfig config_;
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> unit_{0.0, 1.0};
    std::vector<ShadowBook> books_;
    OrderFlow flow_;
    std::uint64_t ref_counter_ = 0;
    std::size_t resting_ = 0;
    std::size_t peak_resting_ = 0;
};

}  // namespace

OrderFlow generate_order_flow(const FlowConfig& config)
{
    return FlowGenerator(config).run();
}
