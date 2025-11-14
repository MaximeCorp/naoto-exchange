#pragma once

namespace MarketExecution {
class Asset {
  private:
    int AssetId;
    int TotalSupply;

  public:
    Asset(int id, int supply);
    ~Asset() = default;

    int getId();
};
} // namespace MarketExecution
