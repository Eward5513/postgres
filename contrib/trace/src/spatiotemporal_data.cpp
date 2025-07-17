#include "../include/spatiotemporal_data.h"
#include "../include/utils.h"
#include <iostream>
#include <cstring>
#include <type_traits>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <random>
#include <limits>

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

// ============================================================================
// SpatioTemporalData Implementations
// ============================================================================

void SpatioTemporalData::print() const {
    std::cout << "SpatioTemporalData: (" << x << ", " << y << ", " << z 
              << "), time: " << time << ", tid: " << static_cast<int>(tid) 
              << ", fid: " << fid << ", pid: " << pid 
              << ", foreign_key: " << foreign_key << std::endl;
}

void swap(SpatioTemporalData& a, SpatioTemporalData& b) noexcept {
    std::swap(a.x, b.x);
    std::swap(a.y, b.y);
    std::swap(a.z, b.z);
    std::swap(a.tid, b.tid);
    std::swap(a.fid, b.fid);
    std::swap(a.pid, b.pid);
    std::swap(a.foreign_key, b.foreign_key);
    std::swap(a.user_id, b.user_id);
    std::swap(a.time, b.time);
    std::swap(a.external_data, b.external_data);
    std::swap(a.is_deleted, b.is_deleted);
}

uint32_t SpatioTemporalData::normalizeCoordinate(float coord, float minCoord, float maxCoord) const {
    const uint32_t maxRange = 0xFFFFFFFF; // 2^32 - 1
    return static_cast<uint32_t>((coord - minCoord) / (maxCoord - minCoord) * maxRange);
}

// ============================================================================
// Bounds Implementations
// ============================================================================

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

// ============================================================================
// SpatialBounds Implementations
// ============================================================================

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

uint64_t indexOfPoint(float x, float y, float z, SpatialBounds bound, int level)
{
    uint64_t x_ = 0, y_ = 0, z_ = 0;
    auto center = bound.getCenter();
    for (auto i=level;i-- > 0;)
    {
        x_ <<= 1;
        y_ <<= 1;
        z_ <<= 1;
        if (x < center.x)
        {
            bound.max.x = center.x;
        }
        else
        {
            bound.min.x = center.x;
            x_ |= 1;
        }
        if (y < center.y)
        {
            bound.max.y = center.y;
        }
        else
        {
            bound.min.y = center.y;
            y_ |= 1;
        }
        if (z < center.z)
        {
            bound.max.z = center.z;
        }
        else
        {
            bound.min.z = center.z;
            z_ |= 1;
        }
        center = bound.getCenter();
    }
    uint64_t index = interleaveBits(x_, y_, z_,level);
    return index;
}

// ============================================================================
// Polygon Implementations
// ============================================================================

bool Polygon::contains(const SpatialPoint& point) const {
    for (const Triangle& triangle : triangles) {
        if (isPointInTriangle(triangle, point, height)) {
            return true;
        }
    }
    return false;
}

bool Polygon::intersects(const SpatialBounds& bound) const {
    for (const Triangle& triangle : triangles) {
        if (isTriangleIntesectBound(triangle, bound, height)) {
            return true;
        }
    }
    return false;
}

SpatialBounds Polygon::getBounds() const {
    if (triangles.empty()) {
        return SpatialBounds();
    }

    SpatialBounds bounds;
    bool first = true;
    
    for (const Triangle& triangle : triangles) {
        SpatialPoint normal = triangle.normal();
        SpatialPoint p1_ext = triangle.p1 + normal * height;
        SpatialPoint p2_ext = triangle.p2 + normal * height;
        SpatialPoint p3_ext = triangle.p3 + normal * height;

        if (first) {
            bounds.min = bounds.max = triangle.p1;
            first = false;
        }
        
        bounds.updateBounds(triangle.p1);
        bounds.updateBounds(triangle.p2);
        bounds.updateBounds(triangle.p3);
        bounds.updateBounds(p1_ext);
        bounds.updateBounds(p2_ext);
        bounds.updateBounds(p3_ext);
    }

    return bounds;
}

Triangle Polygon::generateRandomTriangle(const SpatialBounds& bounds) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    std::uniform_real_distribution<float> dist_x(bounds.min.x, bounds.max.x);
    std::uniform_real_distribution<float> dist_y(bounds.min.y, bounds.max.y);
    std::uniform_real_distribution<float> dist_z(bounds.min.z, bounds.max.z);
    
    SpatialPoint p1(dist_x(gen), dist_y(gen), dist_z(gen));
    SpatialPoint p2(dist_x(gen), dist_y(gen), dist_z(gen));
    SpatialPoint p3(dist_x(gen), dist_y(gen), dist_z(gen));
    
    return Triangle(p1, p2, p3);
}

Triangle Polygon::generateNextTriangle(const SpatialBounds& bounds, const Triangle& previous) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    std::uniform_real_distribution<float> dist_x(bounds.min.x, bounds.max.x);
    std::uniform_real_distribution<float> dist_y(bounds.min.y, bounds.max.y);
    std::uniform_real_distribution<float> dist_z(bounds.min.z, bounds.max.z);
    
    SpatialPoint normal = previous.normal();
    SpatialPoint randomPoint(dist_x(gen), dist_y(gen), dist_z(gen));
    SpatialPoint p3 = randomPoint + normal * (previous.p1 - randomPoint).dot(normal);
    
    return Triangle(previous.p1, previous.p2, p3);
}

Polygon Polygon::generateMesh(const SpatialBounds& bounds, int triangleCount, float height) {
    Polygon mesh;
    mesh.height = height;

    if (triangleCount > 0) {
        Triangle first = generateRandomTriangle(bounds);
        mesh.triangles.push_back(first);

        for (int i = 1; i < triangleCount; ++i) {
            Triangle next = generateNextTriangle(bounds, mesh.triangles.back());
            mesh.triangles.push_back(next);
        }
    }

    return mesh;
}

// ============================================================================
// Trajectory Implementations
// ============================================================================

nlohmann::json Trajectory::to_json() const {
    nlohmann::json json_obj;
    json_obj["id"] = id;
    
    std::vector<std::string> serialized_points;
    for (const auto& point : points) {
        serialized_points.push_back(base64_encode(point.to_binary()));
    }
    json_obj["points"] = serialized_points;
    return json_obj;
}

Trajectory Trajectory::from_json(const nlohmann::json& json_obj) {
    Trajectory traj;
    traj.id = json_obj.at("id").get<int>();
    
    for (const auto& point_str : json_obj.at("points")) {
        traj.points.push_back(SpatioTemporalData::from_binary(base64_decode(point_str.get<std::string>())));
    }
    return traj;
}

void Trajectory::sort_by_timestamp() {
    std::sort(points.begin(), points.end(), [](const SpatioTemporalData& a, const SpatioTemporalData& b) {
        return a.time < b.time;
    });
}

void Trajectory::cut_by_time(float min_time, float max_time) {
    points.erase(std::remove_if(points.begin(), points.end(),
        [min_time, max_time](const SpatioTemporalData& data) {
            return data.time < min_time || data.time > max_time;
        }),
        points.end());
}

// ============================================================================
// Distance Function Implementations
// ============================================================================

float pointToBoundsDistance(const SpatialPoint& point, const SpatialBounds& bounds) {
    float dx = std::max({bounds.min.x - point.x, 0.0f, point.x - bounds.max.x});
    float dy = std::max({bounds.min.y - point.y, 0.0f, point.y - bounds.max.y});
    float dz = std::max({bounds.min.z - point.z, 0.0f, point.z - bounds.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float pointToBoundsMaxDistance(const SpatialPoint& point, const SpatialBounds& bounds) {
    float dx = std::max(std::abs(point.x - bounds.min.x), std::abs(point.x - bounds.max.x));
    float dy = std::max(std::abs(point.y - bounds.min.y), std::abs(point.y - bounds.max.y));
    float dz = std::max(std::abs(point.z - bounds.min.z), std::abs(point.z - bounds.max.z));
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float pointToBoundsDistance(const SpatioTemporalData& point, const SpatialBounds& bounds) {
    float dx = std::max({bounds.min.x - point.x, 0.0f, point.x - bounds.max.x});
    float dy = std::max({bounds.min.y - point.y, 0.0f, point.y - bounds.max.y});
    float dz = std::max({bounds.min.z - point.z, 0.0f, point.z - bounds.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float pointToBoundsDistance(const SpatialPoint& point, const Bounds& bounds) {
    float dx = std::max({bounds.min.x - point.x, 0.0f, point.x - bounds.max.x});
    float dy = std::max({bounds.min.y - point.y, 0.0f, point.y - bounds.max.y});
    float dz = std::max({bounds.min.z - point.z, 0.0f, point.z - bounds.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float pointToBoundsDistance(const SpatioTemporalData& point, const Bounds& bounds) {
    float dx = std::max({bounds.min.x - point.x, 0.0f, point.x - bounds.max.x});
    float dy = std::max({bounds.min.y - point.y, 0.0f, point.y - bounds.max.y});
    float dz = std::max({bounds.min.z - point.z, 0.0f, point.z - bounds.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float calculateDTWDistance(const std::vector<SpatioTemporalData>& traj1, const std::vector<SpatioTemporalData>& traj2) {
    int m = traj1.size();
    int n = traj2.size();
    
    if (m == 0 || n == 0) return std::numeric_limits<float>::infinity();
    
    std::vector<std::vector<float>> dtw(m + 1, std::vector<float>(n + 1, std::numeric_limits<float>::infinity()));
    dtw[0][0] = 0;
    
    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            float cost = pointToPointDistance(traj1[i - 1], traj2[j - 1]);
            dtw[i][j] = cost + std::min({dtw[i - 1][j], dtw[i][j - 1], dtw[i - 1][j - 1]});
        }
    }
    
    return dtw[m][n];
}

float calculateDTWDistance(const Trajectory& traj1, const Trajectory& traj2) {
    return calculateDTWDistance(traj1.points, traj2.points);
}

float calculateCumulativeNearestNeighborDistance(const Trajectory& traj1, const Trajectory& traj2) {
    int m = traj1.points.size();
    int n = traj2.points.size();
    
    if (m == 0 || n == 0) return std::numeric_limits<float>::infinity();

    float totalDistance = 0.0;

    for (int i = 0; i < m; ++i) {
        float minDistance = std::numeric_limits<float>::infinity();
        
        for (int j = 0; j < n; ++j) {
            float distance = pointToPointDistance(traj1.points[i], traj2.points[j]);
            if (distance < minDistance) {
                minDistance = distance;
            }
        }
        
        totalDistance += minDistance;
    }

    return totalDistance / m;
}

float calculateAverageDistance(const Trajectory& traj1, const Trajectory& traj2) {
    if (traj1.points.empty() || traj2.points.empty()) {
        return std::numeric_limits<float>::infinity();
    }
    
    float totalDistance = 0.0;
    int count = 0;
    
    for (const auto& p1 : traj1.points) {
        for (const auto& p2 : traj2.points) {
            totalDistance += pointToPointDistance(p1, p2);
            ++count;
        }
    }
    
    return totalDistance / count;
}

// ============================================================================
// Query Function Implementations
// ============================================================================

std::vector<std::pair<float, Trajectory>> trajectory_kNN_query_bruteforce(
    Trajectory& query_center, int k, float min_time, float max_time, 
    std::unordered_map<int, Trajectory>& data) {
    
    std::vector<std::pair<float, Trajectory>> dataVector;
    dataVector.reserve(data.size());
    
    for (auto& [id, trajectory] : data) {
        dataVector.push_back({0, trajectory});
    }

    const int thread_pool_size = std::thread::hardware_concurrency();
    std::vector<std::future<void>> futures;
    size_t chunk_size = dataVector.size() / thread_pool_size;
    
    for (int i = 0; i < thread_pool_size; ++i) {
        futures.push_back(std::async(std::launch::async, [&, i] {
            size_t start = i * chunk_size;
            size_t end = (i == thread_pool_size - 1) ? dataVector.size() : (i + 1) * chunk_size;
            for (size_t j = start; j < end; ++j) {
                dataVector[j].second.cut_by_time(min_time, max_time);
                dataVector[j].first = calculateDTWDistance(query_center, dataVector[j].second);
            }
        }));
    }

    for (auto& fut : futures) {
        fut.get();
    }

    std::sort(dataVector.begin(), dataVector.end(),
        [](const std::pair<float, Trajectory>& a, const std::pair<float, Trajectory>& b) {
            return a.first < b.first;
        });
    
    if (dataVector.size() > k) {
        dataVector.resize(k);
    }

    return dataVector;
}