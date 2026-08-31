#include "Itch/StockDirectoryMessage.h"

#include "Itch/ItchFields.h"

namespace itch {

bool StockDirectoryMessage::decode(const std::vector<uint8_t>& data)
{
    if (!decode_header(data, kPayloadSize, 'R')) {
        return false;
    }
    copy_ascii_field(data, kStock, stock_);
    market_category_ = static_cast<char>(data[kMarketCategory]);
    return true;
}

}  // namespace itch
