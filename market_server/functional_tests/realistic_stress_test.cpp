#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <order.hpp>
#include <order_confirmation.hpp>
#include <random>
#include <sched.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

using namespace naoto;
using naoto::order_gateway::OrderConfirmation;
using naoto::order_gateway::OrderConfirmationStatus;

namespace
{
    // ---------------------------------------------------------------- wire

#pragma pack(push, 1)
    struct ClientRequest
    {
        char RequestType;
        std::uint32_t ClientId;
        std::array<std::uint8_t, 32> Key;
    };
#pragma pack(pop)

    static_assert(sizeof(ClientRequest) == 37,
                  "ClientRequest must be 37 bytes");

    // Default private key -- the one the gateway authenticates against.
    // NOT the public key stored in ClientState; those are a keypair and
    // sending the public one authenticates into an empty account with
    // no error, which is a miserable thing to debug.
    constexpr std::array<std::uint8_t, 32> DefaultKey = {
        0xaa, 0x92, 0xe9, 0xc3, 0x31, 0x6d, 0xdc, 0x46, 0xdc, 0x88, 0x2b,
        0x45, 0xfe, 0x9f, 0x07, 0xb5, 0x82, 0x30, 0xe8, 0x3f, 0x3e, 0x32,
        0x90, 0xff, 0xbb, 0x5e, 0x2d, 0x6f, 0xa8, 0x0e, 0xbd, 0x4f
    };

    // ---------------------------------------------------------------- config

    struct Config
    {
        std::string Host = "127.0.0.1";
        std::uint16_t Port = 8081;
        std::uint16_t AssetId = 1;
        std::array<std::uint8_t, 32> Key = DefaultKey;

        double DurationSeconds = 30.0;
        double TargetRate = 50000.0; // orders/sec across all bots

        int NbMakers = 4;
        int NbNoise = 4;
        int NbTakers = 4;

        std::int64_t Mid = 100;
        std::int64_t Tick = 1;

        // Relative action weights. A bot's share of TargetRate is
        // proportional to its weight, so changing the mix doesn't
        // change total throughput.
        double MakerWeight = 3.0;
        double NoiseWeight = 1.5;
        double TakerWeight = 1.0;

        std::uint32_t MakerDepth = 20;
        std::uint32_t MakerSize = 10;
        std::uint32_t NoiseDepth = 50; // max ticks from mid
        std::uint32_t NoiseDepthCap = 500; // resting cap
        std::uint32_t NoiseSize = 10;
        double NoiseAggressive = 0.15;
        double NoiseCancel = 0.4;
        std::uint32_t TakerSize = 5;
        std::int64_t MarketBuyMult = 4;

        std::size_t SendBufBytes = 1 << 16;
        std::uint64_t Seed = 0;
        bool PinCore = false;
        int Core = 0;
    };

    // ---------------------------------------------------------------- clock

    inline std::uint64_t NowNs(void) noexcept
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull
            + static_cast<std::uint64_t>(ts.tv_nsec);
    }

    // xoshiro256++ -- the std::mt19937 state is 2.5KB and thrashes L1
    // when 12 bots each hold one. This is 32 bytes and faster.
    class Rng
    {
    public:
        explicit Rng(std::uint64_t seed) noexcept
        {
            for (auto &s : State)
            {
                seed += 0x9E3779B97F4A7C15ull;
                std::uint64_t z = seed;
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                s = z ^ (z >> 31);
            }
        }

        std::uint64_t Next(void) noexcept
        {
            const std::uint64_t result =
                Rotl(State[0] + State[3], 23) + State[0];
            const std::uint64_t t = State[1] << 17;
            State[2] ^= State[0];
            State[3] ^= State[1];
            State[1] ^= State[2];
            State[0] ^= State[3];
            State[2] ^= t;
            State[3] = Rotl(State[3], 45);
            return result;
        }

        // [0, n)
        std::uint32_t Below(std::uint32_t n) noexcept
        {
            return static_cast<std::uint32_t>((Next() >> 32) * n >> 32);
        }

        double Unit(void) noexcept
        {
            return static_cast<double>(Next() >> 11) * 0x1.0p-53;
        }

        // Poisson gaps: real order flow is not evenly spaced, and an
        // evenly-spaced generator hides queueing effects entirely.
        double Exponential(double mean) noexcept
        {
            double u = Unit();
            if (u <= 0.0)
                u = 0x1.0p-53;
            return -mean * std::log(u);
        }

    private:
        static std::uint64_t Rotl(std::uint64_t x, int k) noexcept
        {
            return (x << k) | (x >> (64 - k));
        }

        std::uint64_t State[4];
    };

    // ---------------------------------------------------------------- book

    // The generator's own notion of where the market is. Not the real
    // book -- bots only need a mid to cluster quotes around, so prices
    // land on a moving band of levels rather than uniformly.
    class MidTracker
    {
    public:
        MidTracker(std::int64_t mid, std::int64_t tick) noexcept
            : Mid(mid)
            , Tick(tick)
        {}

        void Drift(Rng &rng) noexcept
        {
            const std::int64_t step =
                static_cast<std::int64_t>(rng.Below(3)) - 1;
            Mid += step * Tick;
            if (Mid < Tick * 2)
                Mid = Tick * 2;
        }

        std::int64_t Get(void) const noexcept
        {
            return Mid;
        }

    private:
        std::int64_t Mid;
        std::int64_t Tick;
    };

    // ---------------------------------------------------------------- stats

    struct Stats
    {
        std::uint64_t Submitted = 0;
        std::uint64_t Cancels = 0;
        std::uint64_t Limits = 0;
        std::uint64_t Markets = 0;
        std::uint64_t Buys = 0;
        std::uint64_t Sells = 0;
        std::uint64_t Confirmed = 0;
        std::uint64_t Accepted = 0;
        std::uint64_t SendBlocked = 0;
        std::uint64_t Rejected[16] = {};
    };

    const char *StatusName(std::uint8_t s) noexcept
    {
        switch (static_cast<OrderConfirmationStatus>(s))
        {
        case OrderConfirmationStatus::Accepted:
            return "Accepted";
        case OrderConfirmationStatus::InsufficientFunds:
            return "InsufficientFunds";
        case OrderConfirmationStatus::MaxPositions:
            return "MaxPositions";
        case OrderConfirmationStatus::InvalidPrice:
            return "InvalidPrice";
        case OrderConfirmationStatus::InvalidQuantity:
            return "InvalidQuantity";
        case OrderConfirmationStatus::UnknownSymbol:
            return "UnknownSymbol";
        case OrderConfirmationStatus::TechnicalFailure:
            return "TechnicalFailure";
        case OrderConfirmationStatus::UserNotConnected:
            return "UserNotConnected";
        case OrderConfirmationStatus::BadClientId:
            return "BadClientId";
        default:
            return "Unknown";
        }
    }

    // ---------------------------------------------------------------- bot

    enum class BotKind : std::uint8_t
    {
        Maker,
        Noise,
        Taker,
    };

    const char *KindName(BotKind k) noexcept
    {
        switch (k)
        {
        case BotKind::Maker:
            return "maker";
        case BotKind::Noise:
            return "noise";
        default:
            return "taker";
        }
    }

    class Bot
    {
    public:
        Bot(std::uint32_t clientId, int fd, BotKind kind, const Config &cfg,
            MidTracker &mid, std::uint64_t seed)
            : ClientId(clientId)
            , Fd(fd)
            , Kind(kind)
            , Cfg(&cfg)
            , Mid(&mid)
            , Rand(seed)
        {
            SendBuf.reserve(cfg.SendBufBytes);
            RecvBuf.reserve(4096);
        }

        std::uint32_t GetClientId(void) const noexcept
        {
            return ClientId;
        }
        int GetFd(void) const noexcept
        {
            return Fd;
        }
        BotKind GetKind(void) const noexcept
        {
            return Kind;
        }
        const Stats &GetStats(void) const noexcept
        {
            return S;
        }
        std::size_t RestingCount(void) const noexcept
        {
            return Resting.size();
        }
        std::uint64_t NextActionNs(void) const noexcept
        {
            return NextAt;
        }
        void SetNextActionNs(std::uint64_t t) noexcept
        {
            NextAt = t;
        }

        double Weight(void) const noexcept
        {
            switch (Kind)
            {
            case BotKind::Maker:
                return Cfg->MakerWeight;
            case BotKind::Noise:
                return Cfg->NoiseWeight;
            default:
                return Cfg->TakerWeight;
            }
        }

        // Makers emit two orders per turn, so their turn rate must be
        // halved for the configured order rate to come out right.
        double OrdersPerAction(void) const noexcept
        {
            return Kind == BotKind::Maker ? 2.0 : 1.0;
        }

        void Act(std::uint64_t now) noexcept
        {
            switch (Kind)
            {
            case BotKind::Maker:
                ActMaker();
                break;
            case BotKind::Noise:
                ActNoise();
                break;
            default:
                ActTaker();
                break;
            }
            NextAt =
                now + static_cast<std::uint64_t>(Rand.Exponential(MeanGapNs));
        }

        void SetMeanGapNs(double gap) noexcept
        {
            MeanGapNs = gap;
        }

        // Returns false if the peer closed.
        bool OnReadable(void) noexcept
        {
            char buf[8192];
            for (;;)
            {
                ssize_t n = ::recv(Fd, buf, sizeof(buf), 0);
                if (n > 0)
                {
                    RecvBuf.insert(RecvBuf.end(), buf, buf + n);
                    if (static_cast<std::size_t>(n) < sizeof(buf))
                        break;
                    continue;
                }
                if (n == 0)
                    return false;
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                if (errno == EINTR)
                    continue;
                return false;
            }

            constexpr std::size_t ConfSize = sizeof(OrderConfirmation);
            std::size_t off = 0;
            while (RecvBuf.size() - off >= ConfSize)
            {
                OrderConfirmation conf{};
                std::memcpy(&conf, RecvBuf.data() + off, ConfSize);
                off += ConfSize;

                ++S.Confirmed;
                const std::uint8_t status =
                    static_cast<std::uint8_t>(conf.Status);
                if (status == 0)
                {
                    ++S.Accepted;
                    // Only now is the order cancellable: the gateway
                    // has assigned it a system id and told us what it
                    // is. Before this the bot has no way to name it.
                    auto it = Pending.find(conf.ClientOrderId);
                    if (it != Pending.end())
                    {
                        Resting.emplace(
                            conf.ClientOrderId,
                            RestingOrder{ conf.OrderId, it->second });
                        RestingFifo.push_back(conf.ClientOrderId);
                        Pending.erase(it);
                    }
                }
                else
                {
                    S.Rejected[status < 16 ? status : 15]++;
                    Pending.erase(conf.ClientOrderId);
                }
            }

            if (off > 0)
                RecvBuf.erase(RecvBuf.begin(), RecvBuf.begin() + off);
            return true;
        }

        // Returns false if the peer closed. Leaves anything unsent in
        // the buffer for the next flush rather than dropping it.
        bool Flush(void) noexcept
        {
            std::size_t off = 0;
            while (off < SendBuf.size())
            {
                ssize_t n = ::send(Fd, SendBuf.data() + off,
                                   SendBuf.size() - off, MSG_NOSIGNAL);
                if (n > 0)
                {
                    off += static_cast<std::size_t>(n);
                    continue;
                }
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                {
                    ++S.SendBlocked;
                    break;
                }
                if (n < 0 && errno == EINTR)
                    continue;
                return false;
            }

            if (off == SendBuf.size())
                SendBuf.clear();
            else if (off > 0)
                SendBuf.erase(SendBuf.begin(), SendBuf.begin() + off);
            return true;
        }

        bool WantsWrite(void) const noexcept
        {
            return !SendBuf.empty();
        }

    private:
        struct RestingOrder
        {
            std::uint32_t OrderId;
            OrderSide Side;
        };

        void Emit(std::int64_t price, std::uint32_t amount, OrderType type,
                  OrderSide side, OrderAction action) noexcept
        {
            const std::uint32_t coid = NextClientOrderId++;

            Order o{};
            o.Price = price;
            o.OrderId = coid;
            o.ClientOrderId = coid;
            o.ClientId = ClientId;
            o.Amount = amount;
            o.AssetId = Cfg->AssetId;
            o.Type = type;
            o.Side = side;
            o.Action = action;

            const auto *bytes = reinterpret_cast<const std::uint8_t *>(&o);
            SendBuf.insert(SendBuf.end(), bytes, bytes + sizeof(Order));

            ++S.Submitted;
            if (action == OrderAction::CANCEL)
                ++S.Cancels;
            else if (type == OrderType::MARKET)
                ++S.Markets;
            else
                ++S.Limits;
            if (side == OrderSide::BUY)
                ++S.Buys;
            else
                ++S.Sells;

            if (action == OrderAction::EXECUTE && type == OrderType::LIMIT)
            {
                Pending.emplace(coid, side);
            }
        }

        bool CancelOldest(void) noexcept
        {
            while (!RestingFifo.empty())
            {
                const std::uint32_t coid = RestingFifo.front();
                auto it = Resting.find(coid);
                if (it == Resting.end())
                {
                    RestingFifo.pop_front();
                    continue;
                }
                // The engine reads the target id out of order.Amount
                // (see CancelOrder in bid_ask.hpp), and Side must match
                // the resting order's side so the right book is walked.
                Emit(0, it->second.OrderId, OrderType::LIMIT, it->second.Side,
                     OrderAction::CANCEL);
                Resting.erase(it);
                RestingFifo.pop_front();
                return true;
            }
            return false;
        }

        std::size_t LiveCount(void) const noexcept
        {
            // An unconfirmed order still occupies a slot at the
            // exchange, so it counts against the cap.
            return Resting.size() + Pending.size();
        }

        void TrimTo(std::size_t cap) noexcept
        {
            int guard = 0;
            while (LiveCount() >= cap && guard < 64)
            {
                if (!CancelOldest())
                    break;
                ++guard;
            }
        }

        void ActMaker(void) noexcept
        {
            TrimTo(Cfg->MakerDepth - 1);

            const std::int64_t mid = Mid->Get();
            const std::int64_t spread =
                Cfg->Tick * static_cast<std::int64_t>(1 + Rand.Below(3));
            const std::int64_t bid =
                (mid - spread) < Cfg->Tick ? Cfg->Tick : (mid - spread);

            Emit(bid, 1 + Rand.Below(Cfg->MakerSize), OrderType::LIMIT,
                 OrderSide::BUY, OrderAction::EXECUTE);
            Emit(mid + spread, 1 + Rand.Below(Cfg->MakerSize), OrderType::LIMIT,
                 OrderSide::SELL, OrderAction::EXECUTE);
        }

        void ActNoise(void) noexcept
        {
            TrimTo(Cfg->NoiseDepthCap);

            const std::int64_t mid = Mid->Get();
            const OrderSide side =
                Rand.Below(2) ? OrderSide::SELL : OrderSide::BUY;

            std::int64_t offset = Cfg->Tick
                * static_cast<std::int64_t>(1 + Rand.Below(Cfg->NoiseDepth));
            // Mostly away from the touch (rests), occasionally through
            // it (crosses and fills).
            if (Rand.Unit() < Cfg->NoiseAggressive)
                offset = -offset;

            std::int64_t price =
                side == OrderSide::BUY ? mid - offset : mid + offset;
            if (price < Cfg->Tick)
                price = Cfg->Tick;

            Emit(price, 1 + Rand.Below(Cfg->NoiseSize), OrderType::LIMIT, side,
                 OrderAction::EXECUTE);

            // Real participants pull quotes that never trade; this is
            // what exercises the OrderMap lookup path.
            if (!Resting.empty() && Rand.Unit() < Cfg->NoiseCancel)
            {
                CancelOldest();
            }
        }

        void ActTaker(void) noexcept
        {
            const OrderSide side =
                Rand.Below(2) ? OrderSide::SELL : OrderSide::BUY;

            // A market BUY is still range-checked against the book in
            // FillBuyOrder (it breaks on BestAskPrice > order.Price),
            // so price 0 never matches. But price is ALSO what the
            // gateway locks funds against, as amount*price of asset 0,
            // so a huge sentinel overflows the risk check and every buy
            // is refused. A modest multiple of mid crosses the book and
            // passes risk. Market SELLs lock Amount only; 0 is fine.
            const std::int64_t price =
                side == OrderSide::BUY ? Mid->Get() * Cfg->MarketBuyMult : 0;

            Emit(price, 1 + Rand.Below(Cfg->TakerSize), OrderType::MARKET, side,
                 OrderAction::EXECUTE);
        }

        std::uint32_t ClientId;
        int Fd;
        BotKind Kind;
        const Config *Cfg;
        MidTracker *Mid;
        Rng Rand;

        std::uint32_t NextClientOrderId = 1;
        std::unordered_map<std::uint32_t, OrderSide> Pending;
        std::unordered_map<std::uint32_t, RestingOrder> Resting;
        std::deque<std::uint32_t> RestingFifo;

        std::vector<std::uint8_t> SendBuf;
        std::vector<std::uint8_t> RecvBuf;

        std::uint64_t NextAt = 0;
        double MeanGapNs = 1000000.0;

        Stats S;
    };

    // ---------------------------------------------------------------- net

    int ConnectAndAuth(const Config &cfg, std::uint32_t clientId)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            std::perror("socket");
            return -1;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(cfg.Port);
        if (::inet_pton(AF_INET, cfg.Host.c_str(), &addr.sin_addr) != 1)
        {
            std::fprintf(stderr, "bad host: %s\n", cfg.Host.c_str());
            ::close(fd);
            return -1;
        }

        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))
            < 0)
        {
            std::perror("connect");
            ::close(fd);
            return -1;
        }

        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        ClientRequest req{};
        req.RequestType = 'A';
        req.ClientId = clientId;
        req.Key = cfg.Key;

        const auto *p = reinterpret_cast<const std::uint8_t *>(&req);
        std::size_t off = 0;
        while (off < sizeof(req))
        {
            ssize_t n = ::send(fd, p + off, sizeof(req) - off, MSG_NOSIGNAL);
            if (n <= 0)
            {
                if (n < 0 && errno == EINTR)
                    continue;
                std::perror("send auth");
                ::close(fd);
                return -1;
            }
            off += static_cast<std::size_t>(n);
        }

        return fd;
    }

    void SetNonBlocking(int fd) noexcept
    {
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    // ---------------------------------------------------------------- args

    bool ParseArgs(int argc, char **argv, Config &cfg)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            auto next = [&](const char *name) -> const char * {
                if (i + 1 >= argc)
                {
                    std::fprintf(stderr, "%s needs a value\n", name);
                    return nullptr;
                }
                return argv[++i];
            };

            if (a == "--host")
            {
                auto v = next("--host");
                if (!v)
                    return false;
                cfg.Host = v;
            }
            else if (a == "--port")
            {
                auto v = next("--port");
                if (!v)
                    return false;
                cfg.Port = static_cast<std::uint16_t>(std::atoi(v));
            }
            else if (a == "--asset")
            {
                auto v = next("--asset");
                if (!v)
                    return false;
                cfg.AssetId = static_cast<std::uint16_t>(std::atoi(v));
            }
            else if (a == "--duration")
            {
                auto v = next("--duration");
                if (!v)
                    return false;
                cfg.DurationSeconds = std::atof(v);
            }
            else if (a == "--rate")
            {
                auto v = next("--rate");
                if (!v)
                    return false;
                cfg.TargetRate = std::atof(v);
            }
            else if (a == "--makers")
            {
                auto v = next("--makers");
                if (!v)
                    return false;
                cfg.NbMakers = std::atoi(v);
            }
            else if (a == "--noise")
            {
                auto v = next("--noise");
                if (!v)
                    return false;
                cfg.NbNoise = std::atoi(v);
            }
            else if (a == "--takers")
            {
                auto v = next("--takers");
                if (!v)
                    return false;
                cfg.NbTakers = std::atoi(v);
            }
            else if (a == "--mid")
            {
                auto v = next("--mid");
                if (!v)
                    return false;
                cfg.Mid = std::atoll(v);
            }
            else if (a == "--tick")
            {
                auto v = next("--tick");
                if (!v)
                    return false;
                cfg.Tick = std::atoll(v);
            }
            else if (a == "--maker-depth")
            {
                auto v = next("--maker-depth");
                if (!v)
                    return false;
                cfg.MakerDepth = static_cast<std::uint32_t>(std::atoi(v));
            }
            else if (a == "--maker-size")
            {
                auto v = next("--maker-size");
                if (!v)
                    return false;
                cfg.MakerSize = static_cast<std::uint32_t>(std::atoi(v));
            }
            else if (a == "--noise-depth")
            {
                auto v = next("--noise-depth");
                if (!v)
                    return false;
                cfg.NoiseDepth = static_cast<std::uint32_t>(std::atoi(v));
            }
            else if (a == "--noise-depth-cap")
            {
                auto v = next("--noise-depth-cap");
                if (!v)
                    return false;
                cfg.NoiseDepthCap = static_cast<std::uint32_t>(std::atoi(v));
            }
            else if (a == "--noise-size")
            {
                auto v = next("--noise-size");
                if (!v)
                    return false;
                cfg.NoiseSize = static_cast<std::uint32_t>(std::atoi(v));
            }
            else if (a == "--taker-size")
            {
                auto v = next("--taker-size");
                if (!v)
                    return false;
                cfg.TakerSize = static_cast<std::uint32_t>(std::atoi(v));
            }
            else if (a == "--market-buy-mult")
            {
                auto v = next("--market-buy-mult");
                if (!v)
                    return false;
                cfg.MarketBuyMult = std::atoll(v);
            }
            else if (a == "--seed")
            {
                auto v = next("--seed");
                if (!v)
                    return false;
                cfg.Seed = std::strtoull(v, nullptr, 10);
            }
            else if (a == "--core")
            {
                auto v = next("--core");
                if (!v)
                    return false;
                cfg.Core = std::atoi(v);
                cfg.PinCore = true;
            }
            else if (a == "--help" || a == "-h")
            {
                std::printf(
                    "usage: %s [options]\n"
                    "  --host H --port P --asset N\n"
                    "  --duration SECONDS   how long to run (default 30)\n"
                    "  --rate ORDERS_PER_SEC  target aggregate rate (default "
                    "50000)\n"
                    "  --makers N --noise N --takers N\n"
                    "  --mid P --tick T --market-buy-mult M\n"
                    "  --maker-depth N --maker-size N\n"
                    "  --noise-depth N --noise-depth-cap N --noise-size N\n"
                    "  --taker-size N --seed S --core CPU\n",
                    argv[0]);
                return false;
            }
            else
            {
                std::fprintf(stderr, "unknown option: %s\n", a.c_str());
                return false;
            }
        }
        return true;
    }
} // namespace

int main(int argc, char **argv)
{
    Config cfg;
    if (!ParseArgs(argc, argv, cfg))
        return 1;

    if (cfg.PinCore)
    {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cfg.Core, &set);
        if (::sched_setaffinity(0, sizeof(set), &set) != 0)
        {
            std::perror("sched_setaffinity");
        }
    }

    const std::uint64_t seed =
        cfg.Seed != 0 ? cfg.Seed : static_cast<std::uint64_t>(NowNs());

    std::vector<BotKind> kinds;
    for (int i = 0; i < cfg.NbMakers; ++i)
        kinds.push_back(BotKind::Maker);
    for (int i = 0; i < cfg.NbNoise; ++i)
        kinds.push_back(BotKind::Noise);
    for (int i = 0; i < cfg.NbTakers; ++i)
        kinds.push_back(BotKind::Taker);

    std::printf("sizeof(Order)=%zu sizeof(OrderConfirmation)=%zu\n",
                sizeof(Order), sizeof(OrderConfirmation));
    std::printf(
        "connecting %zu bots to %s:%u (asset %u), target %.0f/s for %.1fs\n",
        kinds.size(), cfg.Host.c_str(), cfg.Port, cfg.AssetId, cfg.TargetRate,
        cfg.DurationSeconds);

    MidTracker mid(cfg.Mid, cfg.Tick);
    Rng globalRng(seed ^ 0xD1B54A32D192ED03ull);

    std::vector<Bot> bots;
    bots.reserve(kinds.size());

    int epfd = ::epoll_create1(0);
    if (epfd < 0)
    {
        std::perror("epoll_create1");
        return 1;
    }

    for (std::size_t i = 0; i < kinds.size(); ++i)
    {
        const int fd = ConnectAndAuth(cfg, static_cast<std::uint32_t>(i));
        if (fd < 0)
            return 1;
        bots.emplace_back(static_cast<std::uint32_t>(i), fd, kinds[i], cfg, mid,
                          seed + i * 0x9E3779B97F4A7C15ull);
    }

    // The gateway's request-send thread polls two queues with a 75ms
    // timeout each, so an order sent immediately after auth isn't
    // guaranteed to be processed after it. Same wait the reference
    // client does.
    ::usleep(150000);

    std::unordered_map<int, std::size_t> byFd;
    for (std::size_t i = 0; i < bots.size(); ++i)
    {
        SetNonBlocking(bots[i].GetFd());
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.u32 = static_cast<std::uint32_t>(i);
        if (::epoll_ctl(epfd, EPOLL_CTL_ADD, bots[i].GetFd(), &ev) != 0)
        {
            std::perror("epoll_ctl");
            return 1;
        }
        byFd[bots[i].GetFd()] = i;
    }

    // Split the target rate across bots by weight, accounting for the
    // fact that a maker turn emits two orders.
    double totalWeight = 0.0;
    for (const auto &b : bots)
        totalWeight += b.Weight();

    const std::uint64_t start = NowNs();
    for (auto &b : bots)
    {
        const double share = cfg.TargetRate * (b.Weight() / totalWeight);
        const double actionsPerSec = share / b.OrdersPerAction();
        b.SetMeanGapNs(1e9 / (actionsPerSec > 0.0 ? actionsPerSec : 1.0));
        // Stagger first actions so the run doesn't open with every bot
        // firing at once.
        b.SetNextActionNs(start + globalRng.Below(2000000));
    }

    const std::uint64_t deadline =
        start + static_cast<std::uint64_t>(cfg.DurationSeconds * 1e9);

    std::vector<epoll_event> events(bots.size());
    std::vector<bool> dead(bots.size(), false);
    std::size_t nbDead = 0;

    std::uint64_t loops = 0;
    std::uint64_t driftSamples = 0;

    for (;;)
    {
        const std::uint64_t now = NowNs();
        if (now >= deadline)
            break;

        ++loops;

        std::uint64_t nextDue = deadline;
        for (std::size_t i = 0; i < bots.size(); ++i)
        {
            if (dead[i])
                continue;
            auto &b = bots[i];

            // Catch up if we fell behind: without this a slow loop
            // silently lowers the achieved rate and the run quietly
            // measures something other than what was asked for.
            int emitted = 0;
            while (b.NextActionNs() <= now && emitted < 64)
            {
                b.Act(now);
                ++emitted;
            }

            if (b.WantsWrite() && !b.Flush())
            {
                dead[i] = true;
                ++nbDead;
                continue;
            }

            if (b.NextActionNs() < nextDue)
                nextDue = b.NextActionNs();
        }

        if ((loops & 0x3F) == 0)
        {
            mid.Drift(globalRng);
            ++driftSamples;
        }

        // Drain confirmations. Not optional: unread confirmations fill
        // the socket buffers, TCP backpressure reaches the gateway, and
        // its send starts blocking on the hot path -- which would show
        // up as gateway latency that the generator itself caused.
        const std::uint64_t after = NowNs();
        int timeoutMs = 0;
        if (nextDue > after)
        {
            const std::uint64_t waitNs = nextDue - after;
            timeoutMs = waitNs > 1000000ull ? 1 : 0;
        }

        const int n = ::epoll_wait(epfd, events.data(),
                                   static_cast<int>(events.size()), timeoutMs);
        for (int e = 0; e < n; ++e)
        {
            const std::size_t idx = events[e].data.u32;
            if (dead[idx])
                continue;
            if (!bots[idx].OnReadable())
            {
                dead[idx] = true;
                ++nbDead;
                ::epoll_ctl(epfd, EPOLL_CTL_DEL, bots[idx].GetFd(), nullptr);
            }
        }

        if (nbDead == bots.size())
        {
            std::fprintf(stderr, "all connections closed early\n");
            break;
        }
    }

    const std::uint64_t endSend = NowNs();
    const double elapsed = static_cast<double>(endSend - start) / 1e9;

    // Flush whatever is left, then drain confirmations for a moment so
    // the accept/reject counts aren't misleadingly short.
    for (auto &b : bots)
        b.Flush();

    const std::uint64_t drainUntil = NowNs() + 500000000ull;
    while (NowNs() < drainUntil)
    {
        const int n = ::epoll_wait(epfd, events.data(),
                                   static_cast<int>(events.size()), 50);
        for (int e = 0; e < n; ++e)
        {
            const std::size_t idx = events[e].data.u32;
            if (!dead[idx])
                bots[idx].OnReadable();
        }
        if (n == 0)
            break;
    }

    Stats total;
    std::printf("\n%-8s%7s%11s%10s%10s%10s%11s%9s%10s\n", "kind", "client",
                "submitted", "limits", "markets", "cancels", "confirmed",
                "resting", "rejected");
    for (const auto &b : bots)
    {
        const Stats &s = b.GetStats();
        std::uint64_t rej = 0;
        for (std::uint64_t r : s.Rejected)
            rej += r;

        std::printf("%-8s%7u%11" PRIu64 "%10" PRIu64 "%10" PRIu64 "%10" PRIu64
                    "%11" PRIu64 "%9zu%10" PRIu64 "\n",
                    KindName(b.GetKind()), b.GetClientId(), s.Submitted,
                    s.Limits, s.Markets, s.Cancels, s.Confirmed,
                    b.RestingCount(), rej);

        total.Submitted += s.Submitted;
        total.Cancels += s.Cancels;
        total.Limits += s.Limits;
        total.Markets += s.Markets;
        total.Buys += s.Buys;
        total.Sells += s.Sells;
        total.Confirmed += s.Confirmed;
        total.Accepted += s.Accepted;
        total.SendBlocked += s.SendBlocked;
        for (int i = 0; i < 16; ++i)
            total.Rejected[i] += s.Rejected[i];
    }

    std::uint64_t totalRej = 0;
    for (std::uint64_t r : total.Rejected)
        totalRej += r;

    const auto pct = [&](std::uint64_t v) {
        return total.Submitted ? 100.0 * static_cast<double>(v)
                / static_cast<double>(total.Submitted)
                               : 0.0;
    };

    std::printf("\nelapsed:          %.3fs\n", elapsed);
    std::printf("submitted:        %" PRIu64 "\n", total.Submitted);
    std::printf("  limit:          %" PRIu64 " (%.1f%%)\n", total.Limits,
                pct(total.Limits));
    std::printf("  market:         %" PRIu64 " (%.1f%%)\n", total.Markets,
                pct(total.Markets));
    std::printf("  cancel:         %" PRIu64 " (%.1f%%)\n", total.Cancels,
                pct(total.Cancels));
    std::printf("  buy / sell:     %" PRIu64 " / %" PRIu64 "\n", total.Buys,
                total.Sells);
    std::printf("confirmed:        %" PRIu64 "\n", total.Confirmed);
    std::printf("  accepted:       %" PRIu64 " (%.1f%%)\n", total.Accepted,
                pct(total.Accepted));
    std::printf("  rejected:       %" PRIu64 " (%.1f%%)\n", totalRej,
                pct(totalRej));
    std::printf("target rate:      %.0f/s\n", cfg.TargetRate);
    std::printf("achieved rate:    %.0f/s (%.1f%% of target)\n",
                total.Submitted / elapsed,
                100.0 * (total.Submitted / elapsed) / cfg.TargetRate);
    std::printf("send would-block: %" PRIu64 "\n", total.SendBlocked);
    std::printf("loop iterations:  %" PRIu64 "\n", loops);

    if (totalRej > 0)
    {
        std::printf("\nrejections by reason:\n");
        for (int i = 0; i < 16; ++i)
        {
            if (total.Rejected[i] > 0)
            {
                std::printf("  %-20s%" PRIu64 "\n",
                            StatusName(static_cast<std::uint8_t>(i)),
                            total.Rejected[i]);
            }
        }
    }

    for (auto &b : bots)
        ::close(b.GetFd());
    ::close(epfd);
    return 0;
}