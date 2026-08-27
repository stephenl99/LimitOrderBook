#include "Itch/AddOrderMessage.h"
#include "Itch/AddOrderMpidMessage.h"
#include "Itch/CrossTradeMessage.h"
#include "Itch/DecodeMessage.h"
#include "Itch/OrderCancelMessage.h"
#include "Itch/OrderDeleteMessage.h"
#include "Itch/OrderExecutedMessage.h"
#include "Itch/OrderExecutedWithPriceMessage.h"
#include "Itch/OrderReplaceMessage.h"
#include "Itch/TradeMessage.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string stock_view(const char* raw8)
{
    std::string s(raw8, 8);
    while (!s.empty() && s.back() == ' ') {
        s.pop_back();
    }
    return s;
}

void print_header(const itch::Message& m, std::size_t index, std::size_t payload_len)
{
    std::cout << "── [" << index << "] type='" << m.message_type() << "' "
              << "payload_len=" << payload_len
              << " locate=" << m.stock_locate()
              << " track=" << m.tracking_number()
              << " ts_ns=" << m.timestamp_ns() << "\n";
}

void print_message(const itch::Message& m)
{
    switch (m.message_type()) {
    case 'A': {
        const auto& a = static_cast<const itch::AddOrderMessage&>(m);
        std::cout << "    AddOrder  ref=" << a.order_reference_number()
                  << " side=" << a.side()
                  << " shares=" << a.shares()
                  << " stock=" << stock_view(a.stock())
                  << " price=" << a.price() << "\n";
        break;
    }
    case 'F': {
        const auto& a = static_cast<const itch::AddOrderMpidMessage&>(m);
        std::cout << "    AddOrderMPID ref=" << a.order_reference_number()
                  << " side=" << a.side()
                  << " shares=" << a.shares()
                  << " stock=" << stock_view(a.stock())
                  << " price=" << a.price()
                  << " mpid=" << std::string(a.attribution(), 4) << "\n";
        break;
    }
    case 'E': {
        const auto& e = static_cast<const itch::OrderExecutedMessage&>(m);
        std::cout << "    Execute  ref=" << e.order_reference_number()
                  << " executed=" << e.executed_shares()
                  << " match=" << e.match_number() << "\n";
        break;
    }
    case 'C': {
        const auto& e = static_cast<const itch::OrderExecutedWithPriceMessage&>(m);
        std::cout << "    ExecuteWithPrice ref=" << e.order_reference_number()
                  << " executed=" << e.executed_shares()
                  << " match=" << e.match_number()
                  << " printable=" << e.printable()
                  << " exec_price=" << e.execution_price() << "\n";
        break;
    }
    case 'X': {
        const auto& x = static_cast<const itch::OrderCancelMessage&>(m);
        std::cout << "    Cancel   ref=" << x.order_reference_number()
                  << " cancelled=" << x.cancelled_shares() << "\n";
        break;
    }
    case 'D': {
        const auto& d = static_cast<const itch::OrderDeleteMessage&>(m);
        std::cout << "    Delete   ref=" << d.order_reference_number() << "\n";
        break;
    }
    case 'U': {
        const auto& u = static_cast<const itch::OrderReplaceMessage&>(m);
        std::cout << "    Replace  old=" << u.original_order_reference()
                  << " new=" << u.new_order_reference()
                  << " shares=" << u.shares()
                  << " price=" << u.price() << "\n";
        break;
    }
    case 'P': {
        const auto& t = static_cast<const itch::TradeMessage&>(m);
        std::cout << "    TradePrint ref=" << t.order_reference_number()
                  << " side=" << t.side()
                  << " shares=" << t.shares()
                  << " stock=" << stock_view(t.stock())
                  << " price=" << t.price()
                  << " match=" << t.match_number()
                  << "  (not displayed-book)\n";
        break;
    }
    case 'Q': {
        const auto& q = static_cast<const itch::CrossTradeMessage&>(m);
        std::cout << "    CrossTrade shares=" << q.shares()
                  << " stock=" << stock_view(q.stock())
                  << " cross_price=" << q.cross_price()
                  << " match=" << q.match_number()
                  << " cross_type=" << q.cross_type()
                  << "  (not displayed-book)\n";
        break;
    }
    default:
        std::cout << "    (unhandled type)\n";
        break;
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    const char* path = argc > 1 ? argv[1] : "testdata/session_mix.bin";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "itch_dump: cannot open " << path << "\n";
        return 1;
    }

    std::cout << "itch_dump: " << path << "\n";
    std::size_t index = 0;
    std::size_t decoded = 0;
    std::size_t skipped = 0;

    while (true) {
        uint8_t len_buf[2]{};
        in.read(reinterpret_cast<char*>(len_buf), 2);
        if (in.gcount() != 2) {
            break;
        }
        const auto len = static_cast<uint16_t>((len_buf[0] << 8) | len_buf[1]);
        if (len == 0) {
            break;
        }

        std::vector<uint8_t> data(len);
        in.read(reinterpret_cast<char*>(data.data()), len);
        if (in.gcount() != static_cast<std::streamsize>(len)) {
            std::cerr << "itch_dump: truncated frame at index " << index << "\n";
            return 1;
        }

        auto message = itch::decode_message(data);
        if (!message) {
            std::cout << "── [" << index << "] undecoded payload_len=" << len
                      << " type_byte=" << (data.empty() ? '?' : static_cast<char>(data[0]))
                      << "\n";
            ++skipped;
        } else {
            print_header(*message, index, len);
            print_message(*message);
            ++decoded;
        }
        ++index;
    }

    std::cout << "── done: frames=" << index
              << " decoded=" << decoded
              << " skipped=" << skipped << "\n";
    return 0;
}
