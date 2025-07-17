#ifndef SPATIOTEMPORAL_DATA_H
#define SPATIOTEMPORAL_DATA_H

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <cstdint>
#include <type_traits>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>
#include <queue>
#include <future>
#include <unordered_map>
#include <memory>
#include "parameter.h"
#include "nlohmann/json.hpp"

// Forward declarations
struct SpatialBounds;
struct Bounds;
class Trajectory;
class Mesh;
class PointCloud;

// ============================================================================
// Basic Spatial Types
// ============================================================================

struct SpatialPoint {
    float x, y, z;

    SpatialPoint() : x(0), y(0), z(0) {}
    SpatialPoint(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    SpatialPoint operator+(const SpatialPoint& other) const {
        return {x + other.x, y + other.y, z + other.z};
    }

    SpatialPoint operator-(const SpatialPoint& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }

    SpatialPoint operator*(float scalar) const {
        return {x * scalar, y * scalar, z * scalar};
    }

    float dot(const SpatialPoint& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    SpatialPoint cross(const SpatialPoint& other) const {
        return {
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        };
    }

    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    SpatialPoint normalize() const {
        float len = length();
        return {x / len, y / len, z / len};
    }
};

// ============================================================================
// Data Type Enums and Operators
// ============================================================================

enum DataType : uint8_t {
    PointCloudPoint = 0b1,
    MeshPoint = 0b10,
    TrajectoryPoint = 0b100
};

constexpr DataType operator|(DataType lhs, DataType rhs) {
    using UnderlyingType = std::underlying_type_t<DataType>;
    return static_cast<DataType>(
        static_cast<UnderlyingType>(lhs) | static_cast<UnderlyingType>(rhs)
    );
}

inline DataType& operator|=(DataType& lhs, DataType rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr DataType operator&(DataType lhs, DataType rhs) {
    using UnderlyingType = std::underlying_type_t<DataType>;
    return static_cast<DataType>(
        static_cast<UnderlyingType>(lhs) & static_cast<UnderlyingType>(rhs)
    );
}

inline DataType& operator&=(DataType& lhs, DataType rhs) {
    lhs = lhs & rhs;
    return lhs;
}

// ============================================================================
// SpatioTemporalData
// ============================================================================

struct SpatioTemporalData : public SpatialPoint {
    union DataUnion {
        struct {
            float intensity;
        } PointCloud;
        struct {
            float speed;
        } Trajectoy;
        struct {
            uint8_t colorR;
            uint8_t colorG;
            uint8_t colorB;
        } Mesh;
    };

    DataType tid;
    file_id_t fid;
    point_id_t pid;
    foreign_key_id_t foreign_key;
    user_id_t user_id = -1;
    float time;
    DataUnion external_data;
    bool is_deleted = false;

    // Simple constructors
    SpatioTemporalData() : SpatialPoint(), pid(0), fid(0), time(0) {}
    SpatioTemporalData(float x, float y, float z) : SpatialPoint(x, y, z), pid(0), fid(0), time(0) {}
    SpatioTemporalData(float x, float y, float z, float time) : SpatialPoint(x, y, z), time(time), pid(0), fid(0) {}

    // Simple inline functions
    std::string to_binary() const {
        return std::string(reinterpret_cast<const char*>(this), sizeof(*this));
    }

    bool match(std::uint8_t type) const {
        return type & tid;
    }

    uint64_t unique_id() const {
        uint64_t result = 0;
        result |= static_cast<uint64_t>(foreign_key) << 48;
        result |= static_cast<uint64_t>(tid) << (32+13);
        result |= static_cast<uint64_t>(fid) << 32;
        result |= static_cast<uint64_t>(pid) << 0;
        return result;
    }

    bool operator==(const SpatioTemporalData& other) const {
        return foreign_key == other.foreign_key && tid == other.tid && fid == other.fid && pid == other.pid;
    }

    bool operator<(const SpatioTemporalData& other) const {
        if (tid < other.tid) return true;
        if (foreign_key < other.foreign_key) return true;
        if (fid < other.fid) return true;
        if (pid < other.pid) return true;
        return false;
    }

    // Complex functions - declarations only
    static SpatioTemporalData from_binary(const std::string& binary);
    void print_key_elements() const;
    std::string toString() const;
    void print() const;
    
    friend void swap(SpatioTemporalData& a, SpatioTemporalData& b) noexcept;

private:
    uint32_t normalizeCoordinate(float coord, float minCoord, float maxCoord) const;
};

// Hash specialization for SpatioTemporalData
namespace std {
    template<>
    struct hash<SpatioTemporalData> {
        size_t operator()(const SpatioTemporalData& obj) const {
            return hash<std::int64_t>()(obj.tid) ^ hash<std::int64_t>()(obj.fid) && 
                   hash<std::int64_t>()(obj.foreign_key) ^ hash<std::int64_t>()(obj.pid);
        }
    };
}

// ============================================================================
// SpatialBounds
// ============================================================================

struct SpatialBounds {
    SpatialPoint min;
    SpatialPoint max;
    
    // Constructors
    SpatialBounds();
    SpatialBounds(const SpatialPoint& min_, const SpatialPoint& max_);

    // Simple inline operations
    SpatialBounds expand(float delta) const;
    void updateBounds(const SpatialPoint& point);
    SpatialBounds operator&(const SpatialBounds& other) const;
    SpatialBounds operator|(const SpatialBounds& other) const;
    SpatialBounds operator|(const SpatialPoint& point) const;

    SpatialPoint getCenter() const {
        return SpatialPoint(
            (min.x + max.x) / 2.0f,
            (min.y + max.y) / 2.0f,
            (min.z + max.z) / 2.0f
        );
    }

    bool contains(const SpatialPoint& point) const {
        return (point.x >= min.x && point.x <= max.x) &&
               (point.y >= min.y && point.y <= max.y) &&
               (point.z >= min.z && point.z <= max.z);
    }

    bool contains(const SpatialBounds& other) const {
        return (other.min.x >= min.x && other.max.x <= max.x) &&
               (other.min.y >= min.y && other.max.y <= max.y) &&
               (other.min.z >= min.z && other.max.z <= max.z);
    }

    bool intersects(const SpatialBounds& other) const {
        return !(max.x < other.min.x || min.x > other.max.x ||
                 max.y < other.min.y || min.y > other.max.y ||
                 max.z < other.min.z || min.z > other.max.z);
    }

    bool isBoundsValid() const {
        return (max.x >= min.x && max.y >= min.y && max.z >= min.z);
    }

    // Complex functions - declarations only
    static SpatialBounds limit_max();
    static SpatialBounds createBounds(const SpatialPoint& center, float lengthX, float lengthY, float lengthZ);
    SpatialBounds generateOverlap90() const;
    std::vector<SpatialPoint> getVertices() const;
};

// Stream operators for SpatialBounds
std::ostream& operator<<(std::ostream& os, const SpatialBounds& bounds);
std::istream& operator>>(std::istream& is, SpatialBounds& bounds);

// ============================================================================
// Bounds (SpatioTemporal version)
// ============================================================================

struct Bounds {
    SpatioTemporalData min;
    SpatioTemporalData max;
    bool flag;

    // Constructors
    Bounds() : min(SpatioTemporalData()), max(SpatioTemporalData()), flag(true) {}
    Bounds(const SpatioTemporalData& minimum, const SpatioTemporalData& maximum) : min(minimum), max(maximum), flag(false) {}
    Bounds(SpatioTemporalData& p) : min(p), max(p) {}

    // Simple inline functions
    bool isBoundsValid() const {
        return (max.x >= min.x && max.y >= min.y && max.z >= min.z);
    }

    bool contains(const Bounds& other) const {
        return (min.x <= other.min.x && max.x >= other.max.x &&
                min.y <= other.min.y && max.y >= other.max.y &&
                min.z <= other.min.z && max.z >= other.max.z);
    }

    SpatialBounds to_spatial_bound() const {
        SpatialBounds result;
        result.min = min;
        result.max = max;
        return result;
    }

    void updateViaIkdtree(const float min_value[3], const float max_value[3]) {
        min.x = min_value[0];
        min.y = min_value[1];
        min.z = min_value[2];
        max.x = max_value[0];
        max.y = max_value[1];
        max.z = max_value[2];
    }

    SpatioTemporalData getCenter() const {
        return SpatioTemporalData(
            (min.x + max.x) / 2.0f,
            (min.y + max.y) / 2.0f,
            (min.z + max.z) / 2.0f
        );
    }

    float getSize() const {
        float maxX = max.x - min.x;
        float maxY = max.y - min.y;
        float maxZ = max.z - min.z;
        return std::max(std::max(maxX, maxY), maxZ);
    }

    bool intersects(const Bounds& other) const {
        return (min.x <= other.max.x && max.x >= other.min.x) &&
               (min.y <= other.max.y && max.y >= other.min.y) &&
               (min.z <= other.max.z && max.z >= other.min.z);
    }

    bool intersects2(const Bounds& other) const {
        return (min.x <= other.max.x && max.x > other.min.x) &&
               (min.y <= other.max.y && max.y > other.min.y) &&
               (min.z <= other.max.z && max.z > other.min.z);
    }

    bool operator==(const Bounds& other) const {
        return (min.x == other.min.x && min.y == other.min.y && min.z == other.min.z &&
                max.x == other.max.x && max.y == other.max.y && max.z == other.max.z);
    }

    bool operator!=(const Bounds& other) const {
        return !(other == *this);
    }

    Bounds operator&(const Bounds& other) const;
    Bounds operator|(const Bounds& other) const;
    Bounds operator|(const SpatioTemporalData& point) const;

    bool contains(const SpatioTemporalData& point) const {
        return (point.x >= min.x && point.x <= max.x) &&
               (point.y >= min.y && point.y <= max.y) &&
               (point.z >= min.z && point.z <= max.z);
    }

    bool contains(const float x, const float y, const float z, const Bounds& maximum) const {
        return (x >= min.x && x <= max.x) &&
               (y >= min.y && y <= max.y) &&
               (z >= min.z && z <= max.z);
    }

    // Complex functions - declarations only
    static Bounds createBounds(const SpatioTemporalData& center, double width, double height, double depth);
    SpatioTemporalData generateRandomPoint(DataType type = PointCloudPoint) const;
    char getLargestRangeAxis() const;
    static Bounds limit_max();
    void expand(double factor);
    Bounds getChildBounds(int index, const SpatioTemporalData& center) const;
    void update(const SpatioTemporalData& point, bool first_flag = false);
    void update(const void* json_data);
    bool contains2(const float x, const float y, const float z, const Bounds& maximum) const;
    void print() const;
    
    // Friend function declarations
    friend std::istream& operator>>(std::istream& is, Bounds& bounds);
    friend std::ostream& operator<<(std::ostream& os, const Bounds& bounds);
};

// ============================================================================
// Geometric Types
// ============================================================================

struct Triangle {
    SpatialPoint p1, p2, p3;

    Triangle(const SpatialPoint& point1, const SpatialPoint& point2, const SpatialPoint& point3)
        : p1(point1), p2(point2), p3(point3) {}

    SpatialPoint normal() const {
        return ((p2 - p1).cross(p3 - p1)).normalize();
    }
};

struct Polygon {
    std::vector<Triangle> triangles;
    float height;

    bool contains(const SpatialPoint& point) const;
    bool intersects(const SpatialBounds& bound) const;
    SpatialBounds getBounds() const;
    
    // Static factory methods - declarations only
    static Triangle generateRandomTriangle(const SpatialBounds& bounds);
    static Triangle generateNextTriangle(const SpatialBounds& bounds, const Triangle& previous);
    static Polygon generateMesh(const SpatialBounds& bounds, int triangleCount, float height);
};

// Geometric utility functions
bool isPointInTriangle(const Triangle& triangle, const SpatialPoint& point, float height);
bool isTriangleIntesectBound(const Triangle& triangle, const SpatialBounds& bound, float height);
bool isTriangleContainBound(const Triangle& triangle, const SpatialBounds& bound, float height);

// ============================================================================
// Data Collection Classes
// ============================================================================

class Trajectory {
public:
    int id;
    std::vector<SpatioTemporalData> points;

    // Complex functions - declarations only
    nlohmann::json to_json() const;
    static Trajectory from_json(const nlohmann::json& json_obj);
    void sort_by_timestamp();
    void cut_by_time(float min_time, float max_time);
};

class Mesh {
public:
    int id;
    std::vector<SpatioTemporalData> points;
    std::vector<std::vector<int>> connections;
};

class PointCloud {
public:
    int id;
    std::vector<SpatioTemporalData> points;
};

// ============================================================================
// Index and Search Classes - MetricTree moved to metric.h
// ============================================================================

// ============================================================================
// Utility Functions
// ============================================================================

// Indexing functions
uint64_t indexOfPoint(float x, float y, float z, SpatialBounds bound, int level);

// Distance functions
inline float pointToPointDistance(const SpatialPoint& point1, const SpatialPoint& point2) {
    float dx = point1.x - point2.x;
    float dy = point1.y - point2.y;
    float dz = point1.z - point2.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline float pointToPointDistance(const SpatioTemporalData& point1, const SpatioTemporalData& point2) {
    float dx = point1.x - point2.x;
    float dy = point1.y - point2.y;
    float dz = point1.z - point2.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float pointToBoundsDistance(const SpatialPoint& point, const SpatialBounds& bounds);
float pointToBoundsMaxDistance(const SpatialPoint& point, const SpatialBounds& bounds);
float pointToBoundsDistance(const SpatioTemporalData& point, const SpatialBounds& bounds);
float pointToBoundsDistance(const SpatialPoint& point, const Bounds& bounds);
float pointToBoundsDistance(const SpatioTemporalData& point, const Bounds& bounds);

// Trajectory distance functions
float calculateDTWDistance(const std::vector<SpatioTemporalData>& traj1, const std::vector<SpatioTemporalData>& traj2);
float calculateDTWDistance(const Trajectory& traj1, const Trajectory& traj2);
float calculateCumulativeNearestNeighborDistance(const Trajectory& traj1, const Trajectory& traj2);
float calculateAverageDistance(const Trajectory& traj1, const Trajectory& traj2);

// Query functions
std::vector<std::pair<float, Trajectory>> trajectory_kNN_query_bruteforce(
    Trajectory& query_center, int k, float min_time, float max_time, 
    std::unordered_map<int, Trajectory>& data);

#endif // SPATIOTEMPORAL_DATA_H 