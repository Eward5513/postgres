#ifndef MORTON_UTILS_H
#define MORTON_UTILS_H

#include <cstdint>

// 每轴 21-bit 的 Morton 相关工具
uint64_t morton_expand_bits_21(uint32_t v);
uint64_t morton3d_21(uint32_t xi, uint32_t yi, uint32_t zi);
uint32_t quantize21(float v, float vmin, float vmax);
float dequantize21(uint32_t q, float vmin, float vmax);
uint64_t morton_prefix(uint64_t key, int level);
void deinterleave_prefix(uint64_t pref, int level, uint32_t &xi, uint32_t &yi, uint32_t &zi);

#endif // MORTON_UTILS_H


