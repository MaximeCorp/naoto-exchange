#pragma once

#include <client_state.hpp>
#include <local_attempts.hpp>
#include <order.hpp>
#include <order_confirmation.hpp>

namespace naoto::order_gateway
{
    // Extracted verbatim (same logic, same order of checks, same side
    // effects on localAttempt/localSessionId) from what used to be
    // OrderRouter::CheckOrderRisk() in order_router.hpp.
    //
    // Why: OrderRouter's constructor unconditionally calls
    // EtcdClientSetUp(), which does blocking etcd RPCs against a real
    // etcd server (leasekeepalive/set/ls) - there is no way to construct
    // an OrderRouter at all, let alone unit test its private
    // CheckOrderRisk(), without a live etcd instance reachable at
    // ETCD_ADDR. But the actual risk/funds check - the single most
    // safety-critical piece of this class - never touches etcd, sockets,
    // or anything else I/O-related; it only reads already-fetched
    // ClientState and the per-fd local attempt counters. Pulling it out
    // into this free function makes that logic directly unit-testable
    // (see tests/market_server/unit/test_order_risk_check.cpp) without needing any
    // etcd infrastructure, while OrderRouter::CheckOrderRisk() becomes a
    // one-line wrapper around this plus the one piece of state that
    // genuinely belongs to OrderRouter (clearing its per-fd
    // ConfirmationBuffer on a client-id mismatch - that's a resend-buffer
    // concern, not a risk-check concern, so it stays in OrderRouter
    // itself rather than being passed in here).
    template <size_t MaxPositions, size_t MaxAsset>
    [[nodiscard]] OrderConfirmationStatus
    CheckOrderRisk(const ClientState<MaxPositions> &curState,
                    LocalAttempts<MaxPositions> &localAttempt,
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

        uint16_t assetId =
            order.Type == OrderType::MARKET && order.Side == OrderSide::BUY
            ? 0
            : order.AssetId;

        size_t assetIdx;

        bool found = curState.GetAssetIdx(assetId, assetIdx);

        if (!found) [[unlikely]]
        {
            // Think about how to handle missing assetId
            // Should evict an asset that has attempt = 0
            return assetId >= MaxAsset ? OrderConfirmationStatus::UnknownSymbol
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

        // TODO : add the session gen counter to detect new
        // connections and reset local counter
        localAttempt[assetIdx] += amount;

        return OrderConfirmationStatus::Accepted;
    }
} // namespace naoto::order_gateway
