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
        char RequestType; // 'A' = client details / auth request
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
    static_assert(
        sizeof(GatewayConnection) == 2,
        "GatewayConnection layout drifted from spec (expected 2 bytes)");

    // Sent BY a gateway (or an impersonator) TO CDP, after
    // GatewayConnection, to actually make a request on a client's
    // behalf. GatewayId must match the client's authorized gateway or
    // CDP will reject the request -- see the open question in
    // client_details_poller.hpp about which GatewayId that is.
    struct GatewayRequest
    {
        char RequestType; // 'A' = client details request
                          // 'D' = client disconnect
        uint32_t ClientId;
        std::array<uint8_t, 32> Key; // used for client details request only
        uint32_t ClientFd; // gateway-supplied; we're impersonating
                           // the gateway so we own this value --
                           // see note where it's set.
        uint16_t GatewayId; // always needed
    };
    static_assert(
        sizeof(GatewayRequest) == 43,
        "GatewayRequest layout drifted from spec (expected 43 bytes)");

    // Sent BY the dashboard TO a trading gateway, once the session is
    // authenticated, to place OR cancel an order (see Action). For
    // Action::CANCEL, Amount is repurposed to carry the OrderId being
    // cancelled -- per the struct's own upstream comment. Whether
    // Price/Type/Side matter for a cancel isn't confirmed; we still
    // populate them from the current form rather than zeroing them
    // arbitrarily, since nothing says the engine requires zeros there.
    //
    // CONFIRMED against the real header (this replaced an earlier,
    // WRONG version that had OrderId as uint32_t and Type/Side as
    // int32_t-backed enums -- that mismatch alone shifted every field
    // from AssetId onward by 6 bytes, which is exactly why AssetId was
    // showing up as 0 on the gateway's side even though this dashboard
    // was sending the right value at the wrong offset).
    //
    // Timestamp fields (Ingested/Routed/Received) are gated by NAOTO_PERF
    // on the real header -- they are NOT unconditionally present. Same
    // NAOTO_PERF coordination requirement as naoto::OrderStateReport /
    // naoto::OrderBookUpdate in this file: if this ever drifts out of
    // sync with the sending build, the symptom is a struct SIZE
    // mismatch, not a subtle field issue. Never explicitly written by
    // TradingSession::send_order/send_cancel_order (order{} value-inits
    // them to zero either way), so gating them doesn't change behavior,
    // only wire layout.
    enum class OrderType : uint8_t
    {
        LIMIT = 0,
        MARKET = 1,
    };

    enum class OrderSide : uint8_t
    {
        BUY = 0,
        SELL = 1,
    };

    enum class OrderAction : uint8_t
    {
        EXECUTE = 0,
        CANCEL = 1,
    };

    // ASSUMPTION: both OrderId and ClientOrderId are client-supplied and
    // not otherwise specified as needing to differ, so this dashboard
    // sends the SAME locally-generated id for both (see
    // TradingSession::send_order/send_cancel_order). If the gateway
    // expects them to carry different meanings, that's the one place to
    // change.
    struct Order
    {
        int64_t Price;
#ifdef NAOTO_PERF
        uint64_t IngestedTimestamp;
        uint64_t RoutedTimestamp;
        uint64_t ReceivedTimestamp;
#endif
        uint64_t OrderId;
        uint32_t ClientOrderId;
        uint32_t ClientId;
        uint32_t Amount; // used as the target OrderId when Action == CANCEL
        uint16_t AssetId;
        OrderType Type;
        OrderSide Side;
        OrderAction Action;
        std::array<uint8_t, 7> Padding; // explicit -- pack(1) means this
                                        // wouldn't happen implicitly;
                                        // zeroed via the `{}` init used
                                        // at every construction site.
    };
#ifdef NAOTO_PERF
    static_assert(sizeof(Order) == 64,
                  "Order (NAOTO_PERF) layout drifted from spec "
                  "(expected 64 bytes)");
#else
    static_assert(sizeof(Order) == 40,
                  "Order (no NAOTO_PERF) layout drifted from spec "
                  "(expected 40 bytes)");
#endif

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
    static_assert(
        sizeof(OrderConfirmation) == 9,
        "OrderConfirmation layout drifted from spec (expected 9 bytes)");

    inline const char *ToString(OrderConfirmationStatus s)
    {
        switch (s)
        {
        case OrderConfirmationStatus::Accepted:
            return "Accepted";
        case OrderConfirmationStatus::InsufficientFunds:
            return "Insufficient funds";
        case OrderConfirmationStatus::MaxPositions:
            return "Max positions reached";
        case OrderConfirmationStatus::InvalidPrice:
            return "Invalid price";
        case OrderConfirmationStatus::InvalidQuantity:
            return "Invalid quantity";
        case OrderConfirmationStatus::UnknownSymbol:
            return "Unknown symbol";
        case OrderConfirmationStatus::TechnicalFailure:
            return "Technical failure";
        case OrderConfirmationStatus::UserNotConnected:
            return "User not connected";
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
        std::array<int64_t, MaxPositions> Attempt; // pending/reserved
    };
#pragma pack(pop)

} // namespace Gateways

// NAOTO_PERF is a legitimate, sometimes-off build option (see
// CMakeLists.txt's NAOTO_TIMESTAMPS, which mirrors the real project's
// own option and defaults OFF, same as it does there). This dashboard
// handles both states: with it, naoto::OrderStateReport/OrderBookUpdate
// carry the extra timestamp fields the Price History and Trade Feed
// latency features need; without it, those features simply have no
// data to show (see AppState::record_book_update and
// draw_trade_feed_panel in main.cpp for exactly where each is guarded).
// The one thing that DOES matter: whichever way this is set, it must
// match whatever the real gateway/matching-engine build sets, or the
// struct SIZE itself mismatches at runtime -- watch this dashboard's own
// "dropped packet: got N bytes, not a multiple of M" log lines if that
// ever happens.

// ORDER_BOOK_UPDATE_BUY/SELL are plain macros (not scoped constants) in
// the real header -- matched exactly here, not "improved" into an enum,
// since the whole point is matching what's actually on the wire.
#define ORDER_BOOK_UPDATE_BUY 0
#define ORDER_BOOK_UPDATE_SELL 1

namespace naoto
{
#pragma pack(push, 1)
    enum class OrderState : uint32_t
    {
        FILL = 0,
        PARTIAL_FILL = 1,
        CANCEL = 2,
        REJECT = 3,
        ADD = 4,
    };

    // Received via UDP multicast: one per leg of an order-state change.
    // NOT always a completed trade -- State says what actually happened.
    // Deltas are applied DIRECTLY (add, no subtraction) -- sign is
    // already correct for that leg/client.
    //
    // *** NAOTO_PERF must be defined here IF AND ONLY IF it's also
    // defined on whatever machine/build actually sends these structs.
    // Unlike a field being in the wrong ORDER (which at least produces
    // a struct of the same total size, sometimes catchable by inspection),
    // a mismatch here silently changes the struct's SIZE, and every
    // multi-field UDP packet already gets rejected outright by the
    // "not a multiple of sizeof(...)" check at the call site if that
    // happens -- watch the logs for that specific message after
    // rebuilding if timestamps ever look wrong or data stops arriving. ***
    struct OrderStateReport
    {
        int64_t BoughtDelta;
        int64_t SoldDelta;
        int64_t SoldAttemptDelta; // change to the SOLD asset's pending/
                                  // reserved ("Attempt") balance --
                                  // apply directly, like the others.
                                  // No bought-side equivalent is sent.
#ifdef NAOTO_PERF
        uint64_t IngestedTimestamp; // gateway received the order (raw
                                    // rtcd units, NOT necessarily ns --
                                    // see kNsPerRawTimeUnit in main.cpp)
        uint64_t RoutedTimestamp;
        uint64_t ReceivedTimestamp; // matching engine received it
        uint64_t UpdateTimestamp; // market update created / execution time
#endif
        uint32_t SequenceId;
        uint32_t ClientId;
        uint32_t OrderId;
        uint32_t TradeId;
        uint16_t BoughtAssetId;
        uint16_t SoldAssetId;
        OrderState State;
    };
#ifdef NAOTO_PERF
    static_assert(sizeof(OrderStateReport) == 80,
                  "OrderStateReport (NAOTO_PERF) layout drifted from spec "
                  "(expected 80 bytes)");
#else
    static_assert(sizeof(OrderStateReport) == 48,
                  "OrderStateReport (no NAOTO_PERF) layout drifted from spec "
                  "(expected 48 bytes)");
#endif

    // Received via UDP multicast: NEW ABSOLUTE depth at a given
    // (AssetId, Side, Price). Depth == 0 means "remove this price
    // level". No snapshot on connect -- must listen from startup to
    // stay consistent (or bootstrap via some future snapshot API).
    //
    // Same NAOTO_PERF coordination requirement as OrderStateReport above.
    struct OrderBookUpdate
    {
#ifdef NAOTO_PERF
        uint64_t UpdateTimestamp; // when this market update was created
                                  // (raw rtcd units -- see OrderStateReport)
#endif
        int64_t Price;
        uint32_t SequenceId;
        uint32_t Depth;
        uint16_t AssetId;
        uint8_t Side; // 0 = buy, 1 = sell
    };
#ifdef NAOTO_PERF
    static_assert(sizeof(OrderBookUpdate) == 27,
                  "OrderBookUpdate (NAOTO_PERF) layout drifted from spec "
                  "(expected 27 bytes)");
#else
    static_assert(sizeof(OrderBookUpdate) == 19,
                  "OrderBookUpdate (no NAOTO_PERF) layout drifted from spec "
                  "(expected 19 bytes)");
#endif
#pragma pack(pop)

} // namespace naoto