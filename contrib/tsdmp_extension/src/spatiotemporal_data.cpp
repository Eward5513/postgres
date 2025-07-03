#include "../include/spatiotemporal_data.h"
#include <iostream>
#include <cstring>
#include <type_traits>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

// SpatioTemporalData implementations
SpatioTemporalData SpatioTemporalData::from_binary(const std::string& binary) {
    if (binary.size() != sizeof(SpatioTemporalData)) {
        throw std::runtime_error("Binary data size mismatch");
    }
    SpatioTemporalData result;
    std::memcpy(&result, binary.data(), sizeof(SpatioTemporalData));
    return result;
}

void SpatioTemporalData::print_key_elements() const {
    std::cout << "tid: " << tid << std::endl;
    std::cout << "fid: " << fid << std::endl;
    std::cout << "foreign_key: " << foreign_key << std::endl;
    std::cout << "pid: " << pid << std::endl;
}

std::string SpatioTemporalData::toString() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6); // 设置浮点数精度为6位

    // point (x y z)
    oss << "POINT(" << x << " " << y << " " << z << ")";
    // time
    oss << "," << time;
    // tid
    oss << "," << static_cast<int>(tid);
    // fid
    oss << "," << fid;
    // pid
    oss << "," << pid;
    // foreign_key
    oss << "," << foreign_key;
    // userid
    oss << "," << user_id;
    // intensity
    oss << "," << (tid == DataType::PointCloudPoint ? external_data.PointCloud.intensity : 0.0f);
    // speed
    oss << "," << (tid == DataType::TrajectoryPoint ? external_data.Trajectoy.speed : 0.0f);
    // r
    oss << "," << (tid == DataType::MeshPoint ? static_cast<int>(external_data.Mesh.colorR) : 0);
    // g
    oss << "," << (tid == DataType::MeshPoint ? static_cast<int>(external_data.Mesh.colorG) : 0);
    // b
    oss << "," << (tid == DataType::MeshPoint ? static_cast<int>(external_data.Mesh.colorB) : 0);

    return oss.str();
}

void SpatioTemporalData::print() {
    std::cout << x << " / " << y << " / " << z << " / " << " / " << pid
         << " / " << (tid) << " / " << (fid) << std::endl;
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

// Bounds implementations
Bounds Bounds::createBounds(const SpatioTemporalData& center, double width, double height, double depth) {
    Bounds bounds;
    bounds.min.x = center.x - width / 2;
    bounds.min.y = center.y - height / 2;
    bounds.min.z = center.z - depth / 2;
    bounds.max.x = center.x + width / 2;
    bounds.max.y = center.y + height / 2;
    bounds.max.z = center.z + depth / 2;
    return bounds;
}

SpatioTemporalData Bounds::generateRandomPoint(DataType type) const {
    SpatioTemporalData randomPoint;
    randomPoint.tid = type;
    randomPoint.x = min.x + static_cast<float>(rand()) / RAND_MAX * (max.x - min.x);
    randomPoint.y = min.y + static_cast<float>(rand()) / RAND_MAX * (max.y - min.y);
    randomPoint.z = min.z + static_cast<float>(rand()) / RAND_MAX * (max.z - min.z);
    randomPoint.time = min.time + static_cast<float>(rand()) / RAND_MAX * (max.time - min.time);
    return randomPoint;
}

char Bounds::getLargestRangeAxis() const {
    float xRange = std::abs(max.x - min.x);
    float yRange = std::abs(max.y - min.y);
    float zRange = std::abs(max.z - min.z);

    if (xRange >= yRange && xRange >= zRange) {
        return 0;
    } else if (yRange >= xRange && yRange >= zRange) {
        return 1;
    } else {
        return 2;
    }
}

Bounds Bounds::limit_max() {
    Bounds result;
    result.min.x = result.min.y = result.min.z = -std::numeric_limits<float>::max();
    result.max.x = result.max.y = result.max.z = std::numeric_limits<float>::max();
    return result;
}

void Bounds::expand(double factor) {
    // 计算原始的宽度、高度和深度
    float width = max.x - min.x;
    float height = max.y - min.y;
    float depth = max.z - min.z;

    // 算新的宽度、高度和深度
    float newWidth = width * factor / 2.0f;
    float newHeight = height * factor/ 2.0f;
    float newDepth = depth * factor/ 2.0f;

    // 更新min和max
    // 扩展后的min坐标
    min.x = min.x - newWidth;
    min.y = min.y - newHeight;
    min.z = min.z - newDepth;

    // 扩展后的max坐标
    max.x = max.x + newWidth;
    max.y = max.y + newHeight;
    max.z = max.z + newDepth;
}

Bounds Bounds::getChildBounds(int index, const SpatioTemporalData& center) const {
    Bounds childBounds;
    
    // 根据索引确定子边界
    if (index & 1) { // x方向
        childBounds.min.x = center.x;
        childBounds.max.x = max.x;
    } else {
        childBounds.min.x = min.x;
        childBounds.max.x = center.x;
    }
    
    if (index & 2) { // y方向
        childBounds.min.y = center.y;
        childBounds.max.y = max.y;
    } else {
        childBounds.min.y = min.y;
        childBounds.max.y = center.y;
    }
    
    if (index & 4) { // z方向
        childBounds.min.z = center.z;
        childBounds.max.z = max.z;
    } else {
        childBounds.min.z = min.z;
        childBounds.max.z = center.z;
    }
    
    // 时间维度保持不变
    childBounds.min.time = min.time;
    childBounds.max.time = max.time;
    
    return childBounds;
}

void Bounds::update(const SpatioTemporalData& point, bool first_flag) {
    flag = false;
    if(first_flag){
        min.x = point.x;
        min.y = point.y;
        min.z = point.z;
        max.x = point.x;
        max.y = point.y;
        max.z = point.z;
        min.time = point.time;
        max.time = point.time;
        return;
    }
    min.x = std::min(min.x, point.x);
    min.y = std::min(min.y, point.y);
    min.z = std::min(min.z, point.z);
    min.time = std::min(min.time, point.time);

    max.x = std::max(max.x, point.x);
    max.y = std::max(max.y, point.y);
    max.z = std::max(max.z, point.z);
    max.time = std::max(max.time, point.time);
}

void Bounds::update(const void* json_data) {
    // Convert void* to nlohmann::json reference  
    const nlohmann::json& boundJson = *static_cast<const nlohmann::json*>(json_data);
    
    min.x = boundJson["min_x"];
    min.y = boundJson["min_y"];
    min.z = boundJson["min_z"];
    min.time = boundJson["min_time"];
    max.x = boundJson["max_x"];
    max.y = boundJson["max_y"];
    max.z = boundJson["max_z"];
    max.time = boundJson["max_time"];
}

bool Bounds::contains2(const float x, const float y, const float z, const Bounds& maximum) const {
    bool xjudge, yjudge, zjudge;
    if(abs(x - maximum.max.x) < 1e-6) xjudge = (x >= min.x && x <= max.x);
    else xjudge = (x >= min.x && x < max.x);
    if(abs(y - maximum.max.y) < 1e-6) yjudge = (y >= min.y && y <= max.y);
    else yjudge = (y >= min.y && y < max.y);
    if(abs(z - maximum.max.z) < 1e-6) zjudge = (z >= min.z && z <= max.z);
    else zjudge = (z >= min.z && z < max.z);
    return xjudge && yjudge && zjudge;
}

void Bounds::print() const {
    std::cout << min.x << " " << max.x << " / " << min.y << " " << max.y << " / " << min.z << " " << max.z << std::endl;
}

// Stream operators for Bounds
std::istream& operator>>(std::istream& is, Bounds& bounds) {
    is >> bounds.min.x >> bounds.min.y >> bounds.min.z >> bounds.min.time;
    is >> bounds.max.x >> bounds.max.y >> bounds.max.z >> bounds.max.time;
    return is;
}

std::ostream& operator<<(std::ostream& os, const Bounds& bounds) {
    os << bounds.min.x << " " << bounds.min.y << " " << bounds.min.z << " " << bounds.min.time << " ";
    os << bounds.max.x << " " << bounds.max.y << " " << bounds.max.z << " " << bounds.max.time;
    return os;
}

// SpatialBounds implementations
SpatialBounds SpatialBounds::limit_max() {
    SpatialBounds result;
    result.min.x = result.min.y = result.min.z = -std::numeric_limits<float>::max();
    result.max.x = result.max.y = result.max.z = std::numeric_limits<float>::max();
    return result;
}

SpatialBounds SpatialBounds::createBounds(const SpatialPoint& center, float lengthX, float lengthY, float lengthZ) {
    SpatialBounds bounds;
    bounds.min.x = center.x - lengthX / 2.0f;
    bounds.min.y = center.y - lengthY / 2.0f;
    bounds.min.z = center.z - lengthZ / 2.0f;
    bounds.max.x = center.x + lengthX / 2.0f;
    bounds.max.y = center.y + lengthY / 2.0f;
    bounds.max.z = center.z + lengthZ / 2.0f;
    return bounds;
}

SpatialBounds SpatialBounds::generateOverlap90() const {
    SpatialBounds newBound;
    float overlapFactor = 0.9f;
    float minDistX = max.x - min.x;
    float minDistY = max.y - min.y;
    float minDistZ = max.z - min.z;

    // Ensure new bounds are 90% overlapping but not identical
    // Apply the overlapFactor and shift boundaries to avoid exact overlap
    newBound.min.x = min.x + (minDistX * (1 - overlapFactor)) / 2;
    newBound.max.x = max.x - (minDistX * (1 - overlapFactor)) / 2;

    newBound.min.y = min.y + (minDistY * (1 - overlapFactor)) / 2;
    newBound.max.y = max.y - (minDistY * (1 - overlapFactor)) / 2;

    newBound.min.z = min.z + (minDistZ * (1 - overlapFactor)) / 2;
    newBound.max.z = max.z - (minDistZ * (1 - overlapFactor)) / 2;

    return newBound;
}

std::vector<SpatialPoint> SpatialBounds::getVertices() const {
    std::vector<SpatialPoint> vertices;

    // 生成八个顶点的所有组合
    for (int i = 0; i < 8; ++i) {
        float x = (i & 1) ? max.x : min.x;
        float y = (i & 2) ? max.y : min.y;
        float z = (i & 4) ? max.z : min.z;
        vertices.push_back(SpatialPoint(x, y, z));
    }

    return vertices;
}

// Stream operators for SpatialBounds
std::ostream& operator<<(std::ostream& os, const SpatialBounds& bounds) {
    os << bounds.min.x << " " << bounds.min.y << " " << bounds.min.z << " ";
    os << bounds.max.x << " " << bounds.max.y << " " << bounds.max.z;
    return os;
}

std::istream& operator>>(std::istream& is, SpatialBounds& bounds) {
    is >> bounds.min.x >> bounds.min.y >> bounds.min.z;
    is >> bounds.max.x >> bounds.max.y >> bounds.max.z;
    return is;
} 