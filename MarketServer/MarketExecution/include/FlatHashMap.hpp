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

        [[nodiscard]] std::uint64_t hash_64(const K &key) noexcept
        {
            return key * 0x9E3779B97F4A7C15ULL;
        }

        void PaddingSafeWrite(size_t idx, std::uint8_t dib, V *val) noexcept
        {
            Tags[idx] = dib;
            Data[idx] = val;

            if (idx < 16)
            {
                Tags[Size + idx] = dib;
                Data[Size + idx] = val;
            }
        }

        void PaddingSafeSwap(size_t idx, std::uint8_t *dib, V **val) noexcept
        {
            V *temp_val = Data[idx];
            std::uint8_t temp_dib = Tags[idx];

            Data[idx] = *val;
            Tags[idx] = *dib;

            if (idx < 16)
            {
                Data[Size + idx] = *val;
                Tags[Size + idx] = *dib;
            }

            *val = temp_val;
            *dib = temp_dib;
        }

    public:
        FlatHashMap(void)
        {
            Data.fill(nullptr);
            Tags.fill(EMPTY_MARKER);
        }

        [[nodiscard]] V *GetVal(const K &key) noexcept
        {
            size_t cur_idx = (hash_64(key) & (Size - 1)) << 4;
            size_t cur_dib = 0;
            size_t total_size = Size << 4;

            while (cur_dib < total_size)
            {
                __m128i expected_dibs =
                    _mm_add_epi8(base_seq, _mm_set1_epi8(cur_dib));
                __m128i actual_tags =
                    _mm_load_si128((__m128i *)&(Tags[cur_idx]));

                __m128i match_mask = _mm_cmpeq_epi8(actual_tags, expected_dibs);
                __m128i free_slots =
                    _mm_cmpeq_epi8(actual_tags, _mm_set1_epi8(EMPTY_MARKER));
                __m128i richer_slots =
                    _mm_cmplt_epi8(actual_tags, expected_dibs);
                __m128i stop_slots = _mm_or_si128(free_slots, richer_slots);

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
                            (cur_idx + first_match) & (total_size - 1);
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

                cur_dib += 16;
            }

            return nullptr;
        }

        void AddNode(V *val) noexcept
        {
            const K *key = val->GetKey();

            size_t cur_idx = (hash_64(key) & (Size - 1)) << 4;
            size_t cur_dib = 0;

            for (size_t i = 0; cur_dib < 16; ++i)
            {
                if (Tags[cur_idx] == EMPTY_MARKER)
                {
                    PaddingSafeWrite(cur_idx, cur_dib, val);
                    return;
                }
                if (Tags[cur_idx] < cur_dib)
                {
                    PaddingSafeSwap(cur_idx, &cur_dib, &val);
                }

                ++cur_idx;
                ++cur_dib;
            }

            size_t total_size = Size << 4;

            while (cur_dib < total_size)
            {
                while ((cur_idx & 15) != 0)
                {
                    if (Tags[cur_idx] == EMPTY_MARKER)
                    {
                        PaddingSafeWrite(cur_idx, cur_dib, val);
                        return;
                    }
                    if (Tags[cur_idx] < cur_dib)
                    {
                        PaddingSafeSwap(cur_idx, &cur_dib, &val);
                    }

                    cur_idx = (cur_idx + 1) & (total_size - 1);
                    ++cur_dib;
                }

                __m128i expected_dibs =
                    _mm_add_epi8(base_seq, _mm_set1_epi8(cur_dib));
                __m128i actual_tags =
                    _mm_load_si128((__m128i *)&(Tags[(cur_idx)]));

                __m128i free_slots =
                    _mm_cmpeq_epi8(actual_tags, _mm_set1_epi8(EMPTY_MARKER));
                __m128i richer_slots =
                    _mm_cmplt_epi8(actual_tags, expected_dibs);
                __m128i available_slots =
                    _mm_or_si128(free_slots, richer_slots);

                std::uint16_t free = _mm_movemask_epi8(free_slots);
                std::uint16_t available = _mm_movemask_epi8(available_slots);

                std::uint16_t first_free =
                    (free == 0) ? 16 : __builtin_ctz(free);
                std::uint16_t first_available =
                    (available == 0) ? 16 : __builtin_ctz(available);

                if (first_available == 16)
                {
                    cur_idx = (cur_idx + 16) & (total_size - 1);
                    cur_dib += 16;
                    continue;
                }

                size_t slot_idx =
                    (cur_idx + first_available) & (total_size - 1);

                if (first_free == first_available)
                {
                    PaddingSafeWrite(slot_idx, cur_dib + first_available, val);
                    return;
                }
                else
                {
                    cur_idx = slot_idx;
                    cur_dib += first_available;

                    PaddingSafeSwap(cur_idx, &cur_dib, &val);

                    cur_idx = (cur_idx + 1) & (total_size - 1);
                    cur_dib++;

                    continue;
                }

                cur_idx = (cur_idx + 16) & (total_size - 1);
                cur_dib += 16;
            }
            // No available slot
        }
    };
} // namespace MarketExecution
