#include <array>
#include <cstdint>
#include <immintrin.h>

namespace MarketExecution
{
    template <typename K, typename V,
              size_t Size> // CRITICAL: Size MUST be a power of 2
    class FlatHashMap
    {
    private:
        // 16 slots per index (simd alignement) + a 16 slots padding (safety)
        alignas(64) std::array<V *, (Size + 1) * 16> Data;
        alignas(64) std::array<uint8_t, (Size + 1) * 16> Tags;

        constexpr uint8_t EMPTY_MARKER = 0x80;
        static const alignas(16) expr __m128i base_seq =
            _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

        [[nodiscard]] std::uint64_t hash_64(const K &key)
        {
            return key * 0x9E3779B97F4A7C15ULL;
        }

    public:
        FlatHashMap(void)
        {
            Data.fill(nullptr);
            Tags.fill(EMPTY_MARKER);
        }

        [[nodiscard]] V *GetVal(const K &key) noexcept
        {
            size_t offset = 0;
            const std::uint64_t hash = (hash_64(key) & (Size - 1)) << 4;

            const size_t total_size = (Size * 16) - 1;

            while (offset <= total_size)
            {
                __m128i expected_dibs =
                    _mm_add_epi8(base_seq, _mm_set1_epi8(offset));
                __m128i actual_tags =
                    _mm_load_si128((__m128i *)&(Tags[(hash + offset)]));

                __m128i match_mask = _mm_cmpeq_epi8(actual_tags, expected_dibs);
                __m128i empty_slots =
                    _mm_cmpeq_epi8(actual_tags, _mm_set1_epi8(EMPTY_MARKER));
                __m128i richer_slots =
                    _mm_cmplt_epi8(actual_tags, expected_dibs);
                __m128i stop_slots = _mm_or_si128(empty_slots, richer_slots);

                std::uint16_t matches = _mm_movemask_epi8(match_mask);
                std::uint16_t stops = _mm_movemask_epi8(stop_slots);

                std::uint16_t first_stop =
                    (stops == 0) ? 16 : __builtin_ctz(stops);

                while (matches != 0)
                {
                    std::uint16_t first_match =
                        (matches == 0) ? 16 : __builtin_ctz(matches);

                    if (first_stop > first_match)
                    {
                        size_t index_to_check =
                            (hash + offset + first_match) & total_size;
                        if (Data[index_to_check]->GetKey() == key)
                        {
                            return Data[index_to_check];
                        }
                    }
                    else
                    {
                        return nullptr;
                    }

                    matches &= (matches - 1);
                }

                if (stops != 0)
                {
                    return nullptr;
                }

                offset += 16;
            }

            return nullptr;
        }

        void AddNode(V *val) noexcept
        {
            const K *key = val->GetKey();

            size_t offset = 0;
            std::uint64_t hash = (hash_64(key) & (Size - 1)) << 4;

            const size_t total_size = (Size * 16) - 1;

            while (offset <= total_size)
            {
                while ((offset & 15) != 0)
                {
                    if (Tags[hash + offset] == EMPTY_MARKER)
                    {
                        Tags[hash + offset] = offset;
                        Data[hash + offset] = val;
                        return;
                    }

                    if (Tags[hash + offset] < offset)
                    {
                        V *temp = Data[slot_idx];
                        Data[slot_idx] = val;
                        val = temp;
                        std::uint8_t cur_bid = Tags[slot_idx];
                        Tags[slot_idx] = offset + i;
                        hash = slot_idx - cur_bid;
                        offset = cur_bid + 1;
                    }
                }

                for (size_t i = 0; i < 4; ++i)
                {
                    size_t slot_idx = hash + offset + i;
                    if (Tags[slot_idx] == EMPTY_MARKER)
                    {
                        Tags[slot_idx] = offset + i;
                        Data[slot_idx] = val;
                        return;
                    }

                    if (Tags[slot_idx] < offset + i)
                    {
                        V *temp = Data[slot_idx];
                        Data[slot_idx] = val;
                        val = temp;
                        std::uint8_t cur_bid = Tags[slot_idx];
                        Tags[slot_idx] = offset + i;
                        hash = slot_idx - cur_bid;
                        offset = cur_bid + 1;
                    }
                }

                __m128i expected_dibs =
                    _mm_add_epi8(base_seq, _mm_set1_epi8(offset));
                __m128i actual_tags =
                    _mm_load_si128((__m128i *)&(Tags[(hash + offset)]));

                __m128i free_slots =
                    _mm_cmpeq_epi8(actual_tags, _mm_set1_epi8(EMPTY_MARKER));
                __m128i replaceable_slots =
                    _mm_cmplt_epi8(actual_tags, expected_dibs);
                __m128i available_slots =
                    _mm_or_si128(empty_slots, richer_slots);

                std::uint16_t free = _mm_movemask_epi8(free_slots);
                std::uint16_t available = _mm_movemask_epi8(available_slots);

                std::uint16_t first_free =
                    (free == 0) ? 16 : __builtin_ctz(free);
                std::uint16_t first_available =
                    (available == 0) ? 16 : __builtin_ctz(available);

                if (first_available == 16)
                {
                    offset += 16;
                    continue;
                }

                size_t slot_idx = hash + offset + first_available;

                // Handle when the padding is touched (mirror on start)
                if (first_free == first_available)
                {
                    Data[slot_idx] = val;
                    Tags[slot_idx] = offset + first_available;
                    return;
                }
                else
                {
                    V *temp = Data[slot_idx];
                    Data[slot_idx] = val;
                    val = temp;
                    std::uint8_t cur_bid = Tags[slot_idx];
                    Tags[slot_idx] = offset + first_available;
                    hash = slot_idx - cur_bid;
                    offset = cur_bid + 1;
                }
            }

            // No available slot
        }
    };
} // namespace MarketExecution
