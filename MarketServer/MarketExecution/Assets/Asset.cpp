#include "Asset.hpp"

namespace MarketExecution
{
    Asset::Asset(int id, int supply)
        : AssetId(id)
        , TotalSupply(supply)
    {}

    int Asset::getId()
    {
        return AssetId;
    }
} // namespace MarketExecution
