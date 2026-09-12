#pragma once

#include <client_state.hpp>
#include <local_attempts.hpp>
#include <order.hpp>
#include <order_confirmation.hpp>
#include <system_conf.hpp>

namespace naoto::order_gateway
{
    [[nodiscard]] inline OrderConfirmationStatus
    CheckOrderRisk(const ClientState &curState, LocalAttempts &localAttempt,
                   uint32_t &localSessionId, const Order &order,
                   const uint8_t auth) noexcept
    {
        if (!auth) [[unlikely]]
        {
            return OrderConfirmationStatus::UserNotConnected;
        }

        if (curState.SessionId != localSessionId) [[unlikely]]
        {
            localAttempt.Clear();
            localSessionId = curState.SessionId;
        }

        if (curState.ClientId != order.ClientId) [[unlikely]]
        {
            return OrderConfirmationStatus::BadClientId;
        }

        uint16_t assetId = order.Side == OrderSide::BUY ? 0 : order.AssetId;

        size_t assetIdx;

        bool found = curState.GetAssetIdx(assetId, assetIdx);

        if (!found) [[unlikely]]
        {
            // Think about how to handle missing assetId
            // Should evict an asset that has attempt = 0
            return assetId >= MaxAssets ? OrderConfirmationStatus::UnknownSymbol
                                       : OrderConfirmationStatus::MaxPositions;
        }

        int64_t confirmed = curState.GetConfirmedAt(assetIdx);
        int64_t attempt =
            curState.GetAttemptAt(assetIdx) + localAttempt[assetIdx];

        // The maximum needed amount when selling is the amount since it's
        // exactly what we'll spend
        // For buy, LIMIT order allows us to calculate exactly how much will
        // be spent and MARKET orders have a maximum price, giving us an
        // upper bound which we will use to freeze money
        int64_t amount = order.Side == OrderSide::SELL
            ? order.Amount
            : order.Amount * order.Price;

        if (amount > confirmed - attempt) [[unlikely]]
        {
            return OrderConfirmationStatus::InsufficientFunds;
        }

        localAttempt[assetIdx] += amount;

        return OrderConfirmationStatus::Accepted;
    }
} // namespace naoto::order_gateway
