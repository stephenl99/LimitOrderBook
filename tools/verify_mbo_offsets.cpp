#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include "Databento/AddOrderMessage.h"

int main()
{
    std::vector<uint8_t> data(56, 0);

    data[0] = 56;
    data[1] = 0xAB;
    uint16_t publisher_id = 12;
    std::memcpy(&data[2], &publisher_id, 2);
    uint32_t instrument_id = 654321;
    std::memcpy(&data[4], &instrument_id, 4);
    uint64_t ts_event = 1'700'000'000'000'000'000ULL;
    std::memcpy(&data[8], &ts_event, 8);

    uint64_t order_id = 999888777666ULL;
    std::memcpy(&data[16], &order_id, 8);
    int64_t price = 150'000'000'000LL;
    std::memcpy(&data[24], &price, 8);
    uint32_t size = 4200;
    std::memcpy(&data[32], &size, 4);
    data[36] = 0x03;
    data[37] = 7;
    data[38] = 'A';
    data[39] = 'B';
    uint64_t ts_recv = ts_event + 500;
    std::memcpy(&data[40], &ts_recv, 8);
    int32_t ts_in_delta = -1234;
    std::memcpy(&data[48], &ts_in_delta, 4);
    uint32_t sequence = 42;
    std::memcpy(&data[52], &sequence, 4);

    databento::AddOrderMessage msg;
    bool ok = msg.decode(data);

    std::cout << "decode() returned " << (ok ? "true" : "false") << "\n";
    assert(ok);

    std::cout << "instrument_id: expected=" << instrument_id << " got=" << msg.instrument_id() << "\n";
    assert(msg.instrument_id() == instrument_id);

    std::cout << "timestamp_ns (ts_event): expected=" << ts_event << " got=" << msg.timestamp_ns() << "\n";
    assert(msg.timestamp_ns() == ts_event);

    std::cout << "order_id: expected=" << order_id << " got=" << msg.order_id() << "\n";
    assert(msg.order_id() == order_id);

    std::cout << "raw_price: expected=" << price << " got=" << msg.raw_price() << "\n";
    assert(msg.raw_price() == price);

    std::cout << "size: expected=" << size << " got=" << msg.size() << "\n";
    assert(msg.size() == size);

    std::cout << "flags: expected=3 got=" << static_cast<int>(msg.flags()) << "\n";
    assert(msg.flags() == 3);

    std::cout << "channel_id: expected=7 got=" << static_cast<int>(msg.channel_id()) << "\n";
    assert(msg.channel_id() == 7);

    std::cout << "action: expected=Add got_is_add=" << (msg.action() == databento::Action::Add) << "\n";
    assert(msg.action() == databento::Action::Add);

    std::cout << "side: expected=Bid got_is_bid=" << (msg.side() == databento::Side::Bid) << "\n";
    assert(msg.side() == databento::Side::Bid);

    std::cout << "ts_recv: expected=" << ts_recv << " got=" << msg.ts_recv() << "\n";
    assert(msg.ts_recv() == ts_recv);

    std::cout << "ts_in_delta: expected=" << ts_in_delta << " got=" << msg.ts_in_delta() << "\n";
    assert(msg.ts_in_delta() == ts_in_delta);

    std::cout << "sequence: expected=" << sequence << " got=" << msg.sequence() << "\n";
    assert(msg.sequence() == sequence);

    std::cout << "message_type() (should be 'A'): " << msg.message_type() << "\n";
    assert(msg.message_type() == 'A');

    std::cout << "\nALL FIELDS ROUND-TRIPPED CORRECTLY\n";
    return 0;
}
