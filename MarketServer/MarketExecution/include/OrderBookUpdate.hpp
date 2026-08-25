#pragma once

#include <cstdint>

#define ORDER_BOOK_UPDATE_BUY 0
#define ORDER_BOOK_UPDATE_SELL 1

namespace MarketExecution
{
#pragma pack(push, 1)
    struct OrderBookUpdate
    {
        uint32_t SequenceId;
        uint32_t Depth;
        int64_t Price;
        uint16_t AssetId;
        uint8_t Side; // 0 for buy and 1 for sell

        void FillUpdate(uint32_t sequenceId, uint32_t depth, int64_t price,
                        uint16_t assetId, uint8_t side)
        {
            SequenceId = sequenceId;
            Depth = depth;
            Price = price;
            AssetId = assetId;
            Side = side;
        }
    };
#pragma pack(pop)
} // namespace MarketExecution
