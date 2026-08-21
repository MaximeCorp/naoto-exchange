#pragma once
//
// wire_formats.hpp
//
// Exact on-the-wire struct layouts. These MUST match the producing C++
// binaries byte-for-byte. Everything here is #pragma pack(1), native
// (x86) little-endian, raw memcpy'd — no network-byte-order conversion
// on the payloads themselves.
//
// sizeof() static_asserts are included at the bottom of this file so a
// silent ABI drift (someone adds/reorders a field upstream) fails to
// compile instead of silently misparsing every field after the change.
//

#include <array>
#include <cstddef>
#include <cstdint>

namespace Gateways
{
    enum class OrderType : std::int32_t
    {
        LIMIT,
        MARKET
    };

    enum class OrderSide : std::int32_t
    {
        BUY,
        SELL
    };

    constexpr size_t MAX_KEY_LEN = 25; // Not directly used by the wire
                                        // struct below (Key is a fixed
                                        // array<uint8_t,32>); kept here
                                        // for parity with upstream header.

#pragma pack(push, 1)
    // Sent BY the dashboard TO a trading gateway (e.g. from
    // /socket-gateways/<id> in etcd), to open a trading session on a
    // freshly-connected TCP socket, before any Order structs are sent.
    // NOTE: this is a DIFFERENT protocol/connection than talking to CDP
    // directly -- see GatewayConnection/GatewayRequest below for that.
    struct ClientRequest
    {
        char RequestType;            // 'A' = client details / auth request
                                      // 'D' = disconnect
        uint32_t ClientId;
        std::array<uint8_t, 32> Key; // RAW secret; server checks
                                      // SHA-256(Key) against stored hash.
    };
    static_assert(sizeof(ClientRequest) == 37,
                  "ClientRequest layout drifted from spec (expected 37 bytes)");

    // First message a gateway (or anything impersonating one, like this
    // dashboard talking to CDP directly) must send after opening a TCP
    // connection to CDP. Minimal today; may grow more fields later per
    // upstream comment -- re-check sizeof() against the live header
    // before assuming this hasn't changed.
    struct GatewayConnection
    {
        uint16_t GatewayId;
    };
    static_assert(sizeof(GatewayConnection) == 2,
                  "GatewayConnection layout drifted from spec (expected 2 bytes)");

    // Sent BY a gateway (or an impersonator) TO CDP, after
    // GatewayConnection, to actually make a request on a client's
    // behalf. GatewayId must match the client's authorized gateway or
    // CDP will reject the request -- see the open question in
    // client_details_poller.hpp about which GatewayId that is.
    struct GatewayRequest
    {
        char RequestType;            // 'A' = client details request
                                      // 'D' = client disconnect
        uint32_t ClientId;
        std::array<uint8_t, 32> Key; // used for client details request only
        uint32_t ClientFd;           // gateway-supplied; we're impersonating
                                      // the gateway so we own this value --
                                      // see note where it's set.
        uint16_t GatewayId;          // always needed
    };
    static_assert(sizeof(GatewayRequest) == 43,
                  "GatewayRequest layout drifted from spec (expected 43 bytes)");

    // Sent BY the dashboard TO a trading gateway, once the session is
    // authenticated, to place an order.
    struct Order
    {
        uint32_t Id;
        OrderType Type;
        OrderSide Side;
        std::int64_t Price;
        std::int32_t ClientId;
        std::uint32_t Amount;
        std::int32_t Asset;
        std::int64_t Timestamp; // Units unconfirmed (assume ms since
                                 // epoch) -- purely informational for
                                 // this dashboard, we don't rely on it.
    };
    static_assert(sizeof(Order) == 40,
                  "Order layout drifted from spec (expected 40 bytes)");

    enum class OrderConfirmationStatus : uint8_t
    {
        Accepted = 0,
        InsufficientFunds = 1,
        MaxPositions = 2,
        InvalidPrice = 3,
        InvalidQuantity = 4,
        UnknownSymbol = 5,
        TechnicalFailure = 6,
        UserNotConnected = 7,
    };

    // Received BY the dashboard FROM a trading gateway, in response to
    // an Order.
    struct OrderConfirmation
    {
        uint32_t OrderId;
        uint32_t ClientOrderId;
        OrderConfirmationStatus Status;
    };
    static_assert(sizeof(OrderConfirmation) == 9,
                  "OrderConfirmation layout drifted from spec (expected 9 bytes)");

    inline const char *ToString(OrderConfirmationStatus s)
    {
        switch (s)
        {
        case OrderConfirmationStatus::Accepted: return "Accepted";
        case OrderConfirmationStatus::InsufficientFunds: return "Insufficient funds";
        case OrderConfirmationStatus::MaxPositions: return "Max positions reached";
        case OrderConfirmationStatus::InvalidPrice: return "Invalid price";
        case OrderConfirmationStatus::InvalidQuantity: return "Invalid quantity";
        case OrderConfirmationStatus::UnknownSymbol: return "Unknown symbol";
        case OrderConfirmationStatus::TechnicalFailure: return "Technical failure";
        case OrderConfirmationStatus::UserNotConnected: return "User not connected";
        }
        return "Unknown status";
    }

    // ------------------------------------------------------------------
    // CONFIRMED (matches the real struct given). MaxPositions itself is
    // still a runtime/deployment value we have to be told or infer --
    // wrong MaxPositions still silently misparses everything after the
    // arrays since it changes this struct's wire size.
    // ------------------------------------------------------------------
    template <size_t MaxPositions>
    struct ClientRequestResponse
    {
        char Status; // 'A' accepted, 'C' wrong credentials, 'R' refused (other)
        uint64_t SequenceId;
        uint32_t ClientId;
        uint32_t ClientFd;
        std::array<uint16_t, MaxPositions> AssetId;
        std::array<int64_t, MaxPositions> Confirmed; // settled
        std::array<int64_t, MaxPositions> Attempt;   // pending/reserved
    };
#pragma pack(pop)

} // namespace Gateways

namespace MarketExecution
{
#pragma pack(push, 1)
    // Received via UDP multicast: one per leg of a trade (two sent per
    // match, one for each side). Deltas are applied DIRECTLY (add, no
    // subtraction) -- sign is already correct for that leg/client.
    struct OrderStateReport
    {
        uint32_t SequenceId;
        int64_t BoughtDelta; // applied negatively on the *sold* asset,
                              // per the field's own semantics -- i.e.
                              // this struct alone fully describes the
                              // balance change for BoughtAssetId and
                              // SoldAssetId for ClientId.
        int64_t SoldDelta;
        uint32_t ClientId;
        uint32_t OrderId;
        uint32_t TradeId;
        uint16_t BoughtAssetId;
        uint16_t SoldAssetId;
    };
    static_assert(sizeof(OrderStateReport) == 36,
                  "OrderStateReport layout drifted from spec (expected 36 bytes)");

    constexpr uint8_t ORDER_BOOK_UPDATE_BUY = 0;
    constexpr uint8_t ORDER_BOOK_UPDATE_SELL = 1;

    // Received via UDP multicast: NEW ABSOLUTE depth at a given
    // (AssetId, Side, Price). Depth == 0 means "remove this price
    // level". No snapshot on connect -- must listen from startup to
    // stay consistent (or bootstrap via some future snapshot API).
    struct OrderBookUpdate
    {
        uint32_t SequenceId;
        uint32_t Depth;
        int64_t Price;
        uint16_t AssetId;
        uint8_t Side; // 0 = buy, 1 = sell
    };
    static_assert(sizeof(OrderBookUpdate) == 19,
                  "OrderBookUpdate layout drifted from spec (expected 19 bytes)");
#pragma pack(pop)

} // namespace MarketExecution
