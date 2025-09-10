#include "../include/safe_header.h"

#include <cstdint>
#include <cmath>

#include "../include/morton_utils.h"

uint64_t morton_expand_bits_21(uint32_t v)
{
    uint64_t x = v & 0x1fffffULL;        // 21 bits
    x = (x | (x << 32)) & 0x1f00000000ffffULL;
    x = (x | (x << 16)) & 0x1f0000ff0000ffULL;
    x = (x | (x << 8))  & 0x100f00f00f00f00fULL;
    x = (x | (x << 4))  & 0x10c30c30c30c30c3ULL;
    x = (x | (x << 2))  & 0x1249249249249249ULL;
    return x;
}

uint64_t morton3d_21(uint32_t xi, uint32_t yi, uint32_t zi)
{
    return (morton_expand_bits_21(xi) << 2) | (morton_expand_bits_21(yi) << 1) | morton_expand_bits_21(zi);
}

uint32_t quantize21(float v, float vmin, float vmax)
{
    if (v <= vmin) return 0u;
    if (v >= vmax) return 0x1fffffu;
    float t = (v - vmin) / (vmax - vmin);
    uint32_t q = (uint32_t)std::lround(t * 0x1fffff);
    return q > 0x1fffff ? 0x1fffff : q;
}

float dequantize21(uint32_t q, float vmin, float vmax)
{
    float t = (float)q / 0x1fffff;
    return vmin + t * (vmax - vmin);
}

uint64_t morton_prefix(uint64_t key, int level)
{
    if (level <= 0) return 0ULL;
    int used = level * 3;
    int shift = 63 - used;
    if (shift < 0) shift = 0;
    return key >> shift;
}

void deinterleave_prefix(uint64_t pref, int level, uint32_t &xi, uint32_t &yi, uint32_t &zi)
{
    xi = yi = zi = 0u;
    for (int i = 0; i < level; ++i) {
        int from = 3 * (level - 1 - i);
        uint32_t xb = (uint32_t)((pref >> (from + 2)) & 1ULL);
        uint32_t yb = (uint32_t)((pref >> (from + 1)) & 1ULL);
        uint32_t zb = (uint32_t)((pref >> (from + 0)) & 1ULL);
        xi |= (xb << (level - 1 - i));
        yi |= (yb << (level - 1 - i));
        zi |= (zb << (level - 1 - i));
    }
}


