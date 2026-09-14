#!/usr/bin/env python3
"""
market_sim.py

Single-threaded multi-bot market simulator for the naoto matching engine.

Twelve bots on twelve TCP connections, driven by one epoll loop:

  - market makers: quote both sides around a mid, cancel and requote on
    a jittered timer. The cancel-path workload and the source of depth.
  - noise traders: post limits at random distances from the touch. Some
    rest, some cross. Fills out many price levels.
  - takers: market orders at a Poisson rate, alternating sides.

Bots track their own resting orders. Because the gateway assigns
OrderId and reports it back via OrderConfirmation, a bot cannot cancel
an order until that order's confirmation has arrived -- so each bot
keeps a ClientOrderId -> OrderId map and only cancels from it.

RISK MODEL (see CheckOrderRisk in order_risk.hpp) -- the gateway checks
every order against the client's balances BEFORE it reaches the engine:

  - A SELL locks order.Amount of order.AssetId.
  - A BUY locks order.Amount * order.Price of ASSET 0, regardless of
    what AssetId the order names.

So buy prices must stay small enough that amount*price fits inside the
client's asset-0 balance.

Run with --debug-orders N to dump the first N packed orders field by
field, including the raw hex. Compare that against a known-good client
to find wire-format mismatches.
"""

import argparse
import collections
import errno
import random
import select
import socket
import struct
import sys
import time

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8081
DEFAULT_ASSET_ID = 1
DEFAULT_KEY_HEX = "74c689d7454c466c30cb39993debb182b1c25f5827ae8586ddaa92ee7d94d138"

CLIENT_REQUEST_FMT = "<cI32s"        # RequestType, ClientId, Key -- 37 bytes
ORDER_CONFIRMATION_FMT = "<IIB"      # OrderId, ClientOrderId, Status -- 9 bytes
ORDER_CONFIRMATION_SIZE = struct.calcsize(ORDER_CONFIRMATION_FMT)

ORDER_TYPE_LIMIT = 0
ORDER_TYPE_MARKET = 1
ORDER_SIDE_BUY = 0
ORDER_SIDE_SELL = 1
ORDER_ACTION_EXECUTE = 0
ORDER_ACTION_CANCEL = 1

SIDE_NAME = {0: "BUY", 1: "SELL"}
TYPE_NAME = {0: "LIMIT", 1: "MARKET"}
ACTION_NAME = {0: "EXECUTE", 1: "CANCEL"}

CONFIRMATION_STATUS = {
    0: "Accepted", 1: "InsufficientFunds", 2: "MaxPositions",
    3: "InvalidPrice", 4: "InvalidQuantity", 5: "UnknownSymbol",
    6: "TechnicalFailure", 7: "UserNotConnected", 8: "BadClientId",
}

# Set from argv in main(); counts down as orders are dumped.
DEBUG_ORDERS_REMAINING = 0


def order_format(naoto_perf):
    if naoto_perf:
        # Price, Ingested, Routed, Received, OrderId, ClientOrderId,
        # ClientId, Amount, AssetId, Type, Side, Action, Padding[7]
        return "<qQQQQIIIHBBB7s"
    return "<qQIIIHBBB7s"


def describe_order(payload, naoto_perf):
    """Unpack a packed order back out and print every field, so a wire
    mismatch shows up as a field landing somewhere unexpected rather
    than as an opaque rejection code."""
    fields = struct.unpack(order_format(naoto_perf), payload)
    if naoto_perf:
        (price, ingested, routed, received, order_id, client_order_id,
         client_id, amount, asset_id, otype, side, action, _pad) = fields
    else:
        (price, order_id, client_order_id, client_id, amount, asset_id,
         otype, side, action, _pad) = fields
        ingested = routed = received = 0

    print(f"  --- order ({len(payload)} bytes) ---")
    print(f"  Price         {price}")
    if naoto_perf:
        print(f"  Ingested      {ingested}")
        print(f"  Routed        {routed}")
        print(f"  Received      {received}")
    print(f"  OrderId       {order_id}")
    print(f"  ClientOrderId {client_order_id}")
    print(f"  ClientId      {client_id}")
    print(f"  Amount        {amount}")
    print(f"  AssetId       {asset_id}")
    print(f"  Type          {otype} ({TYPE_NAME.get(otype, '?')})")
    print(f"  Side          {side} ({SIDE_NAME.get(side, '?')})")
    print(f"  Action        {action} ({ACTION_NAME.get(action, '?')})")
    print(f"  hex           {payload.hex()}")
    # What CheckOrderRisk will do with this, so a rejection can be
    # predicted from the dump rather than inferred after the fact.
    risk_asset = 0 if side == ORDER_SIDE_BUY else asset_id
    risk_amount = amount if side == ORDER_SIDE_SELL else amount * price
    print(f"  -> risk: locks {risk_amount} of asset {risk_asset}")


def pack_order(naoto_perf, price, order_id, client_order_id, client_id,
               amount, asset_id, order_type, side, action):
    global DEBUG_ORDERS_REMAINING
    if naoto_perf:
        payload = struct.pack(order_format(True), price, 0, 0, 0, order_id,
                              client_order_id, client_id, amount, asset_id,
                              order_type, side, action, b"\x00" * 7)
    else:
        payload = struct.pack(order_format(False), price, order_id,
                              client_order_id, client_id, amount, asset_id,
                              order_type, side, action, b"\x00" * 7)
    if DEBUG_ORDERS_REMAINING > 0:
        DEBUG_ORDERS_REMAINING -= 1
        describe_order(payload, naoto_perf)
    return payload


class Book:
    """The simulator's own view of where the market is. Not the real
    book -- bots only need a mid to quote around, and deriving it from
    their own activity keeps quotes clustered rather than uniform."""

    def __init__(self, initial_mid, tick):
        self.mid = initial_mid
        self.tick = tick

    def drift(self):
        self.mid = max(self.tick * 2,
                       self.mid + random.choice((-1, 0, 1)) * self.tick)


class Bot:
    def __init__(self, client_id, sock, kind, asset_id, naoto_perf, book):
        self.client_id = client_id
        self.sock = sock
        self.kind = kind
        self.asset_id = asset_id
        self.naoto_perf = naoto_perf
        self.book = book

        self.next_client_order_id = 1
        self.pending = {}      # ClientOrderId -> side, awaiting confirmation
        self.resting = {}      # ClientOrderId -> OrderId, confirmed live
        self.recv_buf = b""

        self.sent = 0
        self.confirmed = 0
        self.cancels_sent = 0
        self.rejected = collections.Counter()
        # Rejections split by side, so a one-sided failure is obvious.
        self.rejected_by_side = collections.Counter()
        self.last_side = {}    # ClientOrderId -> side, for attributing rejects

        self.next_action_at = 0.0

    def _send(self, payload):
        try:
            self.sock.sendall(payload)
            self.sent += 1
            return True
        except (BlockingIOError, InterruptedError):
            return False
        except OSError as exc:
            if exc.errno in (errno.EPIPE, errno.ECONNRESET):
                return False
            raise

    def submit(self, price, amount, order_type, side):
        coid = self.next_client_order_id
        self.next_client_order_id += 1
        # OrderId carries the same value as ClientOrderId here. The
        # gateway assigns the real system id and returns it in the
        # confirmation, but a known-good client sends both slots
        # populated, so match that rather than sending 0.
        payload = pack_order(self.naoto_perf, price, coid, coid,
                             self.client_id, amount, self.asset_id,
                             order_type, side, ORDER_ACTION_EXECUTE)
        if self._send(payload):
            self.last_side[coid] = side
            if order_type == ORDER_TYPE_LIMIT:
                self.pending[coid] = side
            return coid
        return None

    def cancel(self, client_order_id):
        """Cancel by system OrderId. The engine reads the target id out
        of order.Amount (see CancelOrder in bid_ask.hpp), and Side must
        match the resting order's side so the right book is searched."""
        order_id = self.resting.get(client_order_id)
        if order_id is None:
            return False
        side = self.pending.get(client_order_id, ORDER_SIDE_BUY)

        coid = self.next_client_order_id
        self.next_client_order_id += 1
        payload = pack_order(self.naoto_perf, 0, coid, coid, self.client_id,
                             order_id, self.asset_id, ORDER_TYPE_LIMIT,
                             side, ORDER_ACTION_CANCEL)
        if not self._send(payload):
            return False

        self.last_side[coid] = side
        self.resting.pop(client_order_id, None)
        self.pending.pop(client_order_id, None)
        self.cancels_sent += 1
        return True

    def live_count(self):
        """Orders the exchange probably still holds: confirmed-resting
        plus anything submitted whose confirmation hasn't arrived."""
        return len(self.resting) + len(self.pending)

    def on_readable(self):
        try:
            chunk = self.sock.recv(65536)
        except (BlockingIOError, InterruptedError):
            return True
        except OSError:
            return False
        if not chunk:
            return False

        self.recv_buf += chunk
        while len(self.recv_buf) >= ORDER_CONFIRMATION_SIZE:
            raw = self.recv_buf[:ORDER_CONFIRMATION_SIZE]
            self.recv_buf = self.recv_buf[ORDER_CONFIRMATION_SIZE:]
            order_id, coid, status = struct.unpack(ORDER_CONFIRMATION_FMT, raw)
            self.confirmed += 1
            if status == 0:
                if coid in self.pending:
                    self.resting[coid] = order_id
            else:
                self.rejected[status] += 1
                side = self.last_side.get(coid)
                if side is not None:
                    self.rejected_by_side[(side, status)] += 1
                self.pending.pop(coid, None)
            self.last_side.pop(coid, None)
        return True

    # -- behaviours ----------------------------------------------------

    def act(self, now, cfg):
        if self.kind == "maker":
            self._act_maker(now, cfg)
        elif self.kind == "noise":
            self._act_noise(now, cfg)
        else:
            self._act_taker(now, cfg)

    def _trim_to(self, cap):
        """Pull oldest confirmed orders until live count is under cap.
        Counts pending as live -- an unconfirmed order still occupies a
        slot at the exchange."""
        guard = 0
        while self.live_count() >= cap and self.resting and guard < 64:
            oldest = next(iter(self.resting))
            if not self.cancel(oldest):
                break
            guard += 1

    def _act_maker(self, now, cfg):
        # Two quotes per turn, so trim to two below the cap or the bot
        # ratchets upward.
        self._trim_to(cfg.maker_depth - 1)

        mid = self.book.mid
        spread = cfg.tick * random.randint(1, 3)
        self.submit(max(cfg.tick, mid - spread),
                    random.randint(1, cfg.maker_size),
                    ORDER_TYPE_LIMIT, ORDER_SIDE_BUY)
        self.submit(mid + spread, random.randint(1, cfg.maker_size),
                    ORDER_TYPE_LIMIT, ORDER_SIDE_SELL)
        # Jittered: without this all makers fire in lockstep and you get
        # a synchronised burst every interval rather than smooth flow.
        self.next_action_at = now + cfg.maker_interval * random.uniform(0.5, 1.5)

    def _act_noise(self, now, cfg):
        self._trim_to(cfg.noise_depth_cap)

        mid = self.book.mid
        side = random.choice((ORDER_SIDE_BUY, ORDER_SIDE_SELL))
        offset = cfg.tick * random.randint(1, cfg.noise_depth)
        if random.random() < cfg.noise_aggressive:
            offset = -offset
        price = mid - offset if side == ORDER_SIDE_BUY else mid + offset
        price = max(cfg.tick, price)
        self.submit(price, random.randint(1, cfg.noise_size),
                    ORDER_TYPE_LIMIT, side)

        if self.resting and random.random() < cfg.noise_cancel:
            self.cancel(random.choice(list(self.resting.keys())))

        self.next_action_at = now + random.expovariate(1.0 / cfg.noise_interval)

    def _act_taker(self, now, cfg):
        side = random.choice((ORDER_SIDE_BUY, ORDER_SIDE_SELL))
        # A market BUY is range-checked against the book in
        # FillBuyOrder (breaks on BestAskPrice > order.Price), so 0
        # never matches -- but the price is ALSO what the gateway locks
        # funds against, as amount*price of asset 0. A modest multiple
        # of mid crosses the book without blowing the risk check.
        # Market SELLs lock Amount only, so 0 is fine there.
        price = self.book.mid * cfg.market_buy_mult \
            if side == ORDER_SIDE_BUY else 0
        self.submit(price, random.randint(1, cfg.taker_size),
                    ORDER_TYPE_MARKET, side)
        self.next_action_at = now + random.expovariate(1.0 / cfg.taker_interval)


def connect_and_auth(host, port, client_id, key):
    sock = socket.create_connection((host, port), timeout=5)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.sendall(struct.pack(CLIENT_REQUEST_FMT, b"A", client_id, key))
    return sock


def main():
    global DEBUG_ORDERS_REMAINING

    p = argparse.ArgumentParser(description="Multi-bot market simulator")
    p.add_argument("--host", default=DEFAULT_HOST)
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("--asset", type=int, default=DEFAULT_ASSET_ID)
    p.add_argument("--key", default=DEFAULT_KEY_HEX)
    p.add_argument("--duration", type=float, default=30.0)
    p.add_argument("--makers", type=int, default=4)
    p.add_argument("--noise", type=int, default=4)
    p.add_argument("--takers", type=int, default=4)
    p.add_argument("--mid", type=int, default=100,
                   help="starting mid price. A BUY locks amount*price of "
                        "asset 0, so keep mid*size small relative to the "
                        "account balance.")
    p.add_argument("--tick", type=int, default=1)
    p.add_argument("--maker-depth", type=int, default=20)
    p.add_argument("--maker-size", type=int, default=10)
    p.add_argument("--maker-interval", type=float, default=0.002)
    p.add_argument("--noise-depth", type=int, default=50)
    p.add_argument("--noise-depth-cap", type=int, default=500)
    p.add_argument("--noise-size", type=int, default=10)
    p.add_argument("--noise-interval", type=float, default=0.004)
    p.add_argument("--noise-aggressive", type=float, default=0.15)
    p.add_argument("--noise-cancel", type=float, default=0.4)
    p.add_argument("--taker-size", type=int, default=5)
    p.add_argument("--taker-interval", type=float, default=0.003)
    p.add_argument("--market-buy-mult", type=int, default=4,
                   help="market BUY price as a multiple of mid")
    p.add_argument("--debug-orders", type=int, default=0,
                   help="dump the first N packed orders field by field, "
                        "with hex and the risk check's view of them")
    p.add_argument("--naoto-perf", dest="naoto_perf", action="store_true",
                   default=True)
    p.add_argument("--no-naoto-perf", dest="naoto_perf", action="store_false")
    p.add_argument("--seed", type=int, default=None)
    cfg = p.parse_args()

    if cfg.seed is not None:
        random.seed(cfg.seed)
    DEBUG_ORDERS_REMAINING = cfg.debug_orders

    try:
        key = bytes.fromhex(cfg.key)
    except ValueError:
        p.error("--key must be valid hex")
    if len(key) != 32:
        p.error(f"--key must be 32 bytes, got {len(key)}")

    kinds = (["maker"] * cfg.makers + ["noise"] * cfg.noise +
             ["taker"] * cfg.takers)
    print(f"Order layout: {'NAOTO_PERF (64B)' if cfg.naoto_perf else '40B'}, "
          f"sizeof(Order)={struct.calcsize(order_format(cfg.naoto_perf))}")
    print(f"Connecting {len(kinds)} bots to {cfg.host}:{cfg.port} "
          f"(asset {cfg.asset})")

    book = Book(cfg.mid, cfg.tick)
    bots = []
    poller = select.epoll()
    by_fd = {}

    for client_id, kind in enumerate(kinds):
        sock = connect_and_auth(cfg.host, cfg.port, client_id, key)
        bot = Bot(client_id, sock, kind, cfg.asset, cfg.naoto_perf, book)
        bots.append(bot)
        by_fd[sock.fileno()] = bot
        poller.register(sock.fileno(), select.EPOLLIN)

    # Matches TradingSession::connect()'s own wait: the gateway's
    # request-send thread polls with a 75ms timeout, so an order sent
    # immediately after auth isn't guaranteed to be processed in order.
    time.sleep(0.15)
    for sock in (b.sock for b in bots):
        sock.setblocking(False)

    start = time.perf_counter()
    deadline = start + cfg.duration
    for bot in bots:
        bot.next_action_at = start + random.uniform(0.0, 0.002)

    dead = set()
    while True:
        now = time.perf_counter()
        if now >= deadline:
            break

        next_due = deadline
        for bot in bots:
            if bot.client_id in dead:
                continue
            if now >= bot.next_action_at:
                bot.act(now, cfg)
            next_due = min(next_due, bot.next_action_at)

        book.drift()

        # Drain confirmations. Not optional: if these go unread the
        # socket buffers fill, TCP backpressure reaches the gateway,
        # and its sendto starts blocking on the hot path.
        timeout = max(0.0, min(next_due - time.perf_counter(), 0.001))
        for fd, _event in poller.poll(timeout):
            bot = by_fd[fd]
            if not bot.on_readable():
                dead.add(bot.client_id)
                poller.unregister(fd)

        if len(dead) == len(bots):
            print("all connections closed early", file=sys.stderr)
            break

    elapsed = time.perf_counter() - start

    drain_until = time.perf_counter() + 0.5
    while time.perf_counter() < drain_until:
        for fd, _event in poller.poll(0.05):
            by_fd[fd].on_readable()

    for bot in bots:
        try:
            bot.sock.close()
        except OSError:
            pass

    total_sent = sum(b.sent for b in bots)
    total_conf = sum(b.confirmed for b in bots)
    total_cancels = sum(b.cancels_sent for b in bots)
    total_rej = sum(sum(b.rejected.values()) for b in bots)

    print(f"\n{'kind':<8}{'client':>7}{'sent':>9}{'cancels':>9}"
          f"{'confirmed':>11}{'resting':>9}{'rejected':>10}")
    for b in bots:
        nrej = sum(b.rejected.values())
        print(f"{b.kind:<8}{b.client_id:>7}{b.sent:>9}{b.cancels_sent:>9}"
              f"{b.confirmed:>11}{len(b.resting):>9}{nrej:>10}")

    print(f"\nelapsed:        {elapsed:.2f}s")
    print(f"orders sent:    {total_sent}")
    print(f"  cancels:      {total_cancels} "
          f"({100.0 * total_cancels / max(1, total_sent):.1f}%)")
    print(f"confirmations:  {total_conf}")
    print(f"rejected:       {total_rej} "
          f"({100.0 * total_rej / max(1, total_sent):.1f}%)")
    print(f"requests/sec:   {total_sent / elapsed:,.0f}")

    rejects = collections.Counter()
    by_side = collections.Counter()
    for b in bots:
        rejects.update(b.rejected)
        by_side.update(b.rejected_by_side)

    if rejects:
        print("\nrejections by reason:")
        for status, count in rejects.most_common():
            print(f"  {CONFIRMATION_STATUS.get(status, status):<20}{count}")
    if by_side:
        # A clean split along side means the two risk lookups behave
        # differently: BUY checks asset 0, SELL checks order.AssetId.
        print("\nrejections by side:")
        for (side, status), count in sorted(by_side.items()):
            print(f"  {SIDE_NAME.get(side, side):<6}"
                  f"{CONFIRMATION_STATUS.get(status, status):<20}{count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())