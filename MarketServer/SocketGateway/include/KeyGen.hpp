
#include <Order.hpp>
#include <StoragePool.hpp>
#include <arpa/inet.h>
#include <array>
#include <cstring>

namespace Gateways
{
    constexpr int ASSET_ID_BITS = 16;
    constexpr int TIMESTAMP_BITS = 42;
    constexpr int MACHINE_ID_BITS = 10;
    constexpr int SEQUENCE_BITS = 12;

    constexpr int TIMESTAMP_SHIFT = MACHINE_ID_BITS + SEQUENCE_BITS;
    constexpr int MACHINE_ID_SHIFT = SEQUENCE_BITS;

    constexpr uint64_t SEQUENCE_MASK = (1ULL << SEQUENCE_BITS) - 1;

    constexpr uint64_t CUSTOM_EPOCH_MS = 1704067200000ULL;

    class KeyGenerator
    {
    private:
        std::uint16_t MachineID;
        std::uint64_t LastTimestamp;
        std::uint16_t Sequence;

        StoragePool<std::array<char, MAX_KEY_LEN>> *Pool;

    public:
        KeyGenerator(std::uint16_t machineID,
                     StoragePool<std::array<char, MAX_KEY_LEN>> *pool);
        std::array<char, MAX_KEY_LEN> *generateKey(Order &order);
    };
}; // namespace Gateways
