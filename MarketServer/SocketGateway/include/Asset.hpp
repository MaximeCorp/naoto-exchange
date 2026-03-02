#pragma once

namespace Gateways
{
    class Asset
    {
    private:
        int AssetId;
        int TotalSupply;

    public:
        Asset(const int id, const int supply);
        ~Asset() = default;

        int getId();
    };
} // namespace Gateways
