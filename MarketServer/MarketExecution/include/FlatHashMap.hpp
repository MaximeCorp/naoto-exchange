#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <immintrin.h>
#include <iostream>

namespace MarketExecution
{
    template <std::integral K, typename V,
              size_t Size> // CRITICAL: Size MUST be a power of 2
        requires std::is_pointer_v<V>
    class FlatHashMap
    {
    private:
        // 16 slots per index (simd alignement) + a 16 slots padding (safety)
        alignas(64) std::array<uint8_t, (Size + 1) * 16> FootPrints;
        alignas(64) std::array<uint8_t, (Size + 1) * 16> Tags;
        alignas(64) std::array<K, (Size + 1) * 16> Keys;
        alignas(64) std::array<V, (Size + 1) * 16> Data;

        // FIXME: This empty market might be confused with an actual dib value
        static constexpr uint8_t EMPTY_MARKER = 0x80;
        alignas(16) inline static const __m128i base_seq =
            _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

        [[nodiscard]] std::uint64_t hash_64(const K &key) noexcept
        {
            return key * 0x9E3779B97F4A7C15ULL;
        }

        void PaddingSafeWrite(const size_t idx, const size_t dib, const K &key,
                              V val) noexcept
        {
            uint8_t h = hash_64(key) >> 56;
            Tags[idx] = dib;
            Data[idx] = val;
            Keys[idx] = key;
            FootPrints[idx] = h;

            if (idx < 16)
            {
                Tags[Size * 16 + idx] = dib;
                Data[Size * 16 + idx] = val;
                Keys[Size * 16 + idx] = key;
                FootPrints[Size * 16 + idx] = h;
            }
        }

        void PaddingSafeSwap(const size_t idx, size_t &dib, K &key,
                             V *val) noexcept
        {
            uint8_t h = hash_64(key) >> 56;
            V temp_val = Data[idx];
            const std::uint8_t temp_dib = Tags[idx];
            const K temp_key = Keys[idx];

            Data[idx] = *val;
            Tags[idx] = dib;
            Keys[idx] = key;
            FootPrints[idx] = h;

            if (idx < 16)
            {
                Data[Size * 16 + idx] = *val;
                Tags[Size * 16 + idx] = dib;
                Keys[Size * 16 + idx] = key;
                FootPrints[Size * 16 + idx] = h;
            }

            *val = temp_val;
            dib = temp_dib;
            key = temp_key;
        }

    public:
        FlatHashMap(void)
        {
            Tags.fill(EMPTY_MARKER);
        }

        [[nodiscard]] V GetVal(const K &key) noexcept
        {
            const uint64_t h = hash_64(key);
            const uint8_t footprint = h >> 56;
            size_t cur_idx = (h & (Size - 1)) << 4;
            size_t cur_dib = 0;
            size_t total_size = Size << 4;

            while (cur_dib < total_size)
            {
                __m128i expected_dibs =
                    _mm_add_epi8(base_seq, _mm_set1_epi8(cur_dib));
                __m128i actual_tags =
                    _mm_load_si128((__m128i *)&(Tags[cur_idx]));
                __m128i actual_footprints =
                    _mm_load_si128((__m128i *)&(FootPrints[cur_idx]));

                __m128i dib_mask = _mm_cmpeq_epi8(actual_tags, expected_dibs);
                __m128i footprint_mask =
                    _mm_cmpeq_epi8(actual_footprints, _mm_set1_epi8(footprint));
                __m128i match_mask = _mm_and_si128(dib_mask, footprint_mask);
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
                        if (FootPrints[index_to_check] == footprint
                            && Keys[index_to_check] == key)
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

        void AddNode(K key, V val) noexcept
        {
            if (GetVal(key)) [[unlikely]]
            {
                return;
            }

            size_t cur_idx = (hash_64(key) & (Size - 1)) << 4;
            size_t cur_dib = 0;

            while (cur_dib < 16)
            {
                if (Tags[cur_idx] == EMPTY_MARKER)
                {
                    PaddingSafeWrite(cur_idx, cur_dib, key, val);
                    return;
                }
                if (Tags[cur_idx] < cur_dib)
                {
                    PaddingSafeSwap(cur_idx, cur_dib, key, &val);
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
                        PaddingSafeWrite(cur_idx, cur_dib, key, val);
                        return;
                    }
                    if (Tags[cur_idx] < cur_dib)
                    {
                        PaddingSafeSwap(cur_idx, cur_dib, key, &val);
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
                    PaddingSafeWrite(slot_idx, cur_dib + first_available, key,
                                     val);
                    return;
                }
                else if (first_available)
                {
                    cur_idx = slot_idx;
                    cur_dib += first_available;

                    PaddingSafeSwap(cur_idx, cur_dib, key, &val);

                    cur_idx = (cur_idx + 1) & (total_size - 1);
                    cur_dib++;

                    continue;
                }

                cur_idx = (cur_idx + 16) & (total_size - 1);
                cur_dib += 16;
            }
            // No available slot
        }

        void DeleteNode(const K &key) noexcept
        {
            const uint64_t h = hash_64(key);
            const uint8_t footprint = hash_64(key) >> 56;
            size_t cur_idx = (h & (Size - 1)) << 4;
            size_t cur_dib = 0;
            size_t total_size = Size << 4;

            while (cur_dib < total_size)
            {
                uint8_t tag = Tags[cur_idx];
                if (tag == EMPTY_MARKER || tag < cur_dib)
                {
                    return;
                }

                if (FootPrints[cur_idx] == footprint && Keys[cur_idx] == key)
                {
                    break;
                }

                cur_idx = (cur_idx + 1) & (total_size - 1);
                ++cur_dib;
            }

            if (cur_dib == total_size)
            {
                return;
            }

            while (true)
            {
                size_t next_idx = (cur_idx + 1) & (total_size - 1);
                uint8_t next_tag = Tags[next_idx];

                if (next_tag == EMPTY_MARKER || next_tag == 0)
                {
                    break;
                }

                PaddingSafeWrite(cur_idx, next_tag - 1, Keys[next_idx],
                                 Data[next_idx]);
                cur_idx = next_idx;
            }

            PaddingSafeWrite(cur_idx, EMPTY_MARKER, 0, nullptr);
        }

        void DebugDump(void) noexcept
        {
            const size_t total_size = Size << 4;

            std::cout << "=== FlatHashMap dump (" << total_size
                      << " slots) ===\n";

            size_t count = 0;
            for (size_t i = 0; i < total_size; ++i)
            {
                if (Tags[i] == EMPTY_MARKER)
                {
                    continue;
                }

                std::cout << "[idx=" << i
                          << "] dib=" << static_cast<int>(Tags[i])
                          << " footprint=" << static_cast<int>(FootPrints[i])
                          << " key=" << Keys[i] << " -> ";

                if (Data[i])
                {
                    Data[i]->log();
                }
                else
                {
                    std::cout << "nullptr\n";
                }

                ++count;
            }

            std::cout << "=== " << count << " entries total ===\n";
        }
    };
} // namespace MarketExecution
