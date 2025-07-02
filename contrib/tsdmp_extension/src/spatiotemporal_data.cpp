#include "../include/spatiotemporal_data.h"
#include <iostream>
#include <cstring>
#include <type_traits>

void SpatioTemporalData::print() {
    std::cout << x << " / " << y << " / " << z << " / " << " / " << pid
         << " / " << (tid) << " / " << (fid) << std::endl;
}

uint32_t SpatioTemporalData::normalizeCoordinate(float coord, float minCoord, float maxCoord) const {
    const uint32_t maxRange = 0xFFFFFFFF; // 2^32 - 1
    return static_cast<uint32_t>((coord - minCoord) / (maxCoord - minCoord) * maxRange); // Min-max 归一化
}


void swap(SpatioTemporalData& a, SpatioTemporalData& b) noexcept {
    static_assert(std::is_trivially_copyable_v<SpatioTemporalData>,
                  "SpatioTemporalData must be trivially copyable for memcpy-based swap.");
    static_assert(std::is_trivially_destructible_v<SpatioTemporalData>,
                  "SpatioTemporalData must be trivially destructible for memcpy-based swap.");

    char temp[sizeof(SpatioTemporalData)];
    std::memcpy(temp, &a, sizeof(SpatioTemporalData)); // 将 a 拷贝到临时空间
    std::memcpy(&a, &b, sizeof(SpatioTemporalData));   // 将 b 拷贝到 a
    std::memcpy(&b, temp, sizeof(SpatioTemporalData)); // 将临时空间拷贝到 b
} 