#include <KeyGen.hpp>

uint64_t htonll(uint64_t hostval)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return __builtin_bswap64(hostval);
#else
    return hostval;
#endif
};

uint64_t ntohll(uint64_t netval)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return __builtin_bswap64(netval);
#else
    return netval;
#endif
}

namespace Gateways
{
    KeyGenerator::KeyGenerator(std::uint16_t machineID,
                               StoragePool<std::array<char, MAX_KEY_LEN>> *pool)
        : MachineID(machineID)
        , LastTimestamp(0)
        , Sequence(0)
        , Pool(pool)
    {
        if (machineID >= (1ULL << MACHINE_ID_BITS))
        {
            throw std::runtime_error("Machine ID too large.");
        }
    }
    std::array<char, MAX_KEY_LEN> *KeyGenerator::generateKey(Order &order)
    {
        auto now = std::chrono::steady_clock::now();
        uint64_t currentTimestamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();

        uint64_t timestamp = currentTimestamp - CUSTOM_EPOCH_MS;

        if (timestamp < LastTimestamp)
        {
            throw std::runtime_error(
                "Clock moved backwards. Refusing to generate ID.");
        }

        if (timestamp == LastTimestamp)
        {
            Sequence = (Sequence + 1) & SEQUENCE_MASK;

            if (Sequence == 0)
            {
                while (currentTimestamp <= LastTimestamp + CUSTOM_EPOCH_MS)
                {
                    currentTimestamp =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
                }
                timestamp = currentTimestamp - CUSTOM_EPOCH_MS;
            }
        }
        else
        {
            Sequence = 0;
        }

        LastTimestamp = timestamp;

        std::array<char, MAX_KEY_LEN> *result = Pool->acquire();

        uint16_t BigEndianAssetID = htons(order.getAsset());
        std::memcpy(result->data(), &BigEndianAssetID, ASSET_ID_BYTES);

        uint64_t snowkey = htonll((timestamp << TIMESTAMP_SHIFT)
                                  | (MachineID << MACHINE_ID_SHIFT) | Sequence);

        std::memcpy(result->data() + sizeof(uint16_t), &snowkey,
                    sizeof(uint64_t));

        return result;
    }
} // namespace Gateways
