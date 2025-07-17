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
#include "parameter.h"

// Forward declaration for SpatialBounds
struct SpatialBounds;

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

// 重载 |= 运算符
inline DataType& operator|=(DataType& lhs, DataType rhs) {
    lhs = lhs | rhs;
    return lhs;
}

// 重载 & 运算符
constexpr DataType operator&(DataType lhs, DataType rhs) {
    using UnderlyingType = std::underlying_type_t<DataType>;
    return static_cast<DataType>(
        static_cast<UnderlyingType>(lhs) & static_cast<UnderlyingType>(rhs)
    );
}

// 重载 &= 运算符
inline DataType& operator&=(DataType& lhs, DataType rhs) {
    lhs = lhs & rhs;
    return lhs;
}

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

    std::string to_binary() const {
        return std::string(reinterpret_cast<const char*>(this), sizeof(*this));
    }

    // Simple inline functions
    SpatioTemporalData() : SpatialPoint(),pid(0), fid(0),time(0) {}
    SpatioTemporalData(float x, float y, float z) : SpatialPoint(x, y, z), pid(0), fid(0),time(0) {}
    SpatioTemporalData(float x, float y, float z,float time) : SpatialPoint(x, y, z),time(time), pid(0), fid(0) {}

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
        if (tid < other.tid) {return true;}
        if (foreign_key < other.foreign_key) {return true;}
        if (fid < other.fid) {return true;}
        if (pid < other.pid) {return true;}
        return false;
    }

    // Complex functions - declarations only
    static SpatioTemporalData from_binary(const std::string& binary);
    void print_key_elements() const;
    std::string toString() const;
    void print();
    
    friend void swap(SpatioTemporalData& a, SpatioTemporalData& b) noexcept;

private:
    uint32_t normalizeCoordinate(float coord, float minCoord, float maxCoord) const {
        const uint32_t maxRange = 0xFFFFFFFF; // 2^32 - 1
        return static_cast<uint32_t>((coord - minCoord) / (maxCoord - minCoord) * maxRange); // Min-max 归一化
    }
};

namespace std {
    template<>
    struct hash<SpatioTemporalData> {
        size_t operator()(const SpatioTemporalData& obj) const {
            return hash<std::int64_t>()(obj.tid) ^ hash<std::int64_t>()(obj.fid) && hash<std::int64_t>()(obj.foreign_key) ^ hash<std::int64_t>()(obj.pid);
        }
    };
}

struct SpatialBounds {
    SpatialPoint min;
    SpatialPoint max;
    
    // Simple inline functions
    SpatialBounds()
        : min(std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max()),
          max(std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest()) {}

    SpatialBounds(const SpatialPoint& min_, const SpatialPoint& max_)
        : min(min_), max(max_) {}

    SpatialBounds expand(float delta) const {
        SpatialBounds expandedBounds = *this;
        expandedBounds.min.x -= delta;
        expandedBounds.min.y -= delta;
        expandedBounds.min.z -= delta;
        expandedBounds.max.x += delta;
        expandedBounds.max.y += delta;
        expandedBounds.max.z += delta;
        return expandedBounds;
    }

    void updateBounds(const SpatialPoint& point) {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }

    SpatialBounds operator&(const SpatialBounds& other) const {
        SpatialBounds result = *this;
        result.min.x = std::max(min.x, other.min.x);
        result.min.y = std::max(min.y, other.min.y);
        result.min.z = std::max(min.z, other.min.z);
        result.max.x = std::min(max.x, other.max.x);
        result.max.y = std::min(max.y, other.max.y);
        result.max.z = std::min(max.z, other.max.z);
        return result;
    }

    SpatialBounds operator|(const SpatialBounds& other) const {
        SpatialBounds result = *this;
        result.min.x = std::min(min.x, other.min.x);
        result.min.y = std::min(min.y, other.min.y);
        result.min.z = std::min(min.z, other.min.z);
        result.max.x = std::max(max.x, other.max.x);
        result.max.y = std::max(max.y, other.max.y);
        result.max.z = std::max(max.z, other.max.z);
        return result;
    }

    SpatialBounds operator|(const SpatialPoint& point) const {
        SpatialBounds result = *this;
        result.min.x = std::min(min.x, point.x);
        result.min.y = std::min(min.y, point.y);
        result.min.z = std::min(min.z, point.z);
        result.max.x = std::max(max.x, point.x);
        result.max.y = std::max(max.y, point.y);
        result.max.z = std::max(max.z, point.z);
        return result;
    }

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

// 流出运算符 (输出) 的声明
std::ostream& operator<<(std::ostream& os, const SpatialBounds& bounds);

// 流入运算符 (输入) 的声明
std::istream& operator>>(std::istream& is, SpatialBounds& bounds);

uint64_t indexOfPoint(float x, float y, float z, SpatialBounds bound, int level);

struct Bounds {
    SpatioTemporalData min;
    SpatioTemporalData max;
    bool flag;

    // Simple inline functions
    Bounds() : min(SpatioTemporalData()), max(SpatioTemporalData()), flag(true) {}
    Bounds(const SpatioTemporalData& minimum, const SpatioTemporalData& maximum) : min(minimum), max(maximum), flag(false) {}
    Bounds(SpatioTemporalData &p):min(p),max(p) {}

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

    // Complex functions - declarations only
    static Bounds createBounds(const SpatioTemporalData& center, double width, double height, double depth);
    SpatioTemporalData generateRandomPoint(DataType type = PointCloudPoint) const;
    char getLargestRangeAxis() const;
    static Bounds limit_max();
    void expand(double factor);
    Bounds getChildBounds(int index, const SpatioTemporalData& center) const;
    void update(const SpatioTemporalData& point, bool first_flag = false);
    
    // Update bounds from JSON data (using void* to avoid header dependency)
    void update(const void* json_data);

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

    Bounds operator&(const Bounds& other) const {
        Bounds result = *this;
        result.min.x = std::max(min.x, other.min.x);
        result.min.y = std::max(min.y, other.min.y);
        result.min.z = std::max(min.z, other.min.z);
        result.max.x = std::min(max.x, other.max.x);
        result.max.y = std::min(max.y, other.max.y);
        result.max.z = std::min(max.z, other.max.z);
        return result;
    }

    Bounds operator|(const Bounds& other) const {
        Bounds result = *this;
        result.min.x = std::min(min.x, other.min.x);
        result.min.y = std::min(min.y, other.min.y);
        result.min.z = std::min(min.z, other.min.z);
        result.max.x = std::max(max.x, other.max.x);
        result.max.y = std::max(max.y, other.max.y);
        result.max.z = std::max(max.z, other.max.z);
        return result;
    }

    Bounds operator|(const SpatioTemporalData& point) const {
        Bounds result = *this;
        result.min.x = std::min(min.x, point.x);
        result.min.y = std::min(min.y, point.y);
        result.min.z = std::min(min.z, point.z);
        result.max.x = std::max(max.x, point.x);
        result.max.y = std::max(max.y, point.y);
        result.max.z = std::max(max.z, point.z);
        return result;
    }

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

    bool contains2(const float x, const float y, const float z, const Bounds& maximum) const;
    void print() const;
    
    // Friend function declarations
    friend std::istream& operator>>(std::istream& is, Bounds& bounds);
    friend std::ostream& operator<<(std::ostream& os, const Bounds& bounds);
};

class Trajectory{
    public:
    int id;
    std::vector<SpatioTemporalData> points;

    nlohmann::json to_json() const {
        nlohmann::json json_obj;
        json_obj["id"] = id;
        // 序列化每个 SpatioTemporalData 到 Base64
        std::vector<std::string> serialized_points;
        for (const auto& point : points) {
            serialized_points.push_back(base64_encode(point.to_binary()));
        }
        json_obj["points"] = serialized_points;
        return json_obj;
    }

    // 从 JSON 反序列化
    static Trajectory from_json(const nlohmann::json& json_obj) {
        Trajectory traj;
        traj.id = json_obj.at("id").get<int>();
        // 反序列化 Base64 到 SpatioTemporalData
        for (const auto& point_str : json_obj.at("points")) {
            traj.points.push_back(SpatioTemporalData::from_binary(base64_decode(point_str.get<std::string>())));
        }
        return traj;
    }

    void sort_by_timestamp(){
        std::sort(points.begin(), points.end(), [](const SpatioTemporalData& a, const SpatioTemporalData& b) {
            return a.time < b.time;
        });
    }
    void cut_by_time(float min_time,float max_time){
        points.erase(std::remove_if(points.begin(), points.end(),
            [min_time, max_time](const SpatioTemporalData& data) {
                return data.time < min_time || data.time > max_time;
            }),
            points.end());
    }
};


class Mesh{
    public:
    int id;
    std::vector<SpatioTemporalData> points;
    std::vector<std::vector<int>> connections;
};


class PointCloud{
    public:
    int id;
    std::vector<SpatioTemporalData> points;
};

struct SpatialBounds {
    public:
 
    SpatialPoint min;
    SpatialPoint max;
    SpatialBounds()
        : min(std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max()),
          max(std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest()) {}
    SpatialBounds expand(float delta) const {
        SpatialBounds expandedBounds = *this;
        expandedBounds.min.x -= delta;
        expandedBounds.min.y -= delta;
        expandedBounds.min.z -= delta;

        expandedBounds.max.x += delta;
        expandedBounds.max.y += delta;
        expandedBounds.max.z += delta;

        return expandedBounds;
    }
    SpatialBounds(const SpatialPoint& min_, const SpatialPoint& max_)
        : min(min_), max(max_) {}
    SpatialBounds generateOverlap90() {
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
    static SpatialBounds limit_max(){
        SpatialBounds result;
        result.min.x = result.min.y = result.min.z = -std::numeric_limits<float>::max();
        result.max.x = result.max.y = result.max.z = std::numeric_limits<float>::max();
        return result;
    }

    void updateBounds(const SpatialPoint& point) {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }

     SpatialBounds operator&(const SpatialBounds& other) const {
        SpatialBounds result = *this;
        result.min.x = std::max(min.x, other.min.x);
        result.min.y = std::max(min.y, other.min.y);
        result.min.z = std::max(min.z, other.min.z);
        result.max.x = std::min(max.x, other.max.x);
        result.max.y = std::min(max.y, other.max.y);
        result.max.z = std::min(max.z, other.max.z);
        return result;
    }

    static SpatialBounds createBounds(const SpatialPoint& center, float lengthX, float lengthY, float lengthZ) {
        SpatialBounds bounds;

        // 计算最小点（中心点 - 长度的一半）
        bounds.min.x = center.x - lengthX / 2.0f;
        bounds.min.y = center.y - lengthY / 2.0f;
        bounds.min.z = center.z - lengthZ / 2.0f;

        // 计算最大点（中心点 + 长度的一半）
        bounds.max.x = center.x + lengthX / 2.0f;
        bounds.max.y = center.y + lengthY / 2.0f;
        bounds.max.z = center.z + lengthZ / 2.0f;

        return bounds;
    }

    SpatialBounds operator|(const SpatialBounds& other) const {
        SpatialBounds result = *this;
        result.min.x = std::min(min.x, other.min.x);
        result.min.y = std::min(min.y, other.min.y);
        result.min.z = std::min(min.z, other.min.z);
        result.max.x = std::max(max.x, other.max.x);
        result.max.y = std::max(max.y, other.max.y);
        result.max.z = std::max(max.z, other.max.z);
        return result;
    }

    SpatialBounds operator|(const SpatialPoint& point) const {
        SpatialBounds result = *this;
        result.min.x = std::min(min.x, point.x);
        result.min.y = std::min(min.y, point.y);
        result.min.z = std::min(min.z, point.z);
        result.max.x = std::max(max.x, point.x);
        result.max.y = std::max(max.y, point.y);
        result.max.z = std::max(max.z, point.z);
        return result;
    }


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

    std::vector<SpatialPoint> getVertices() const {
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

    bool isBoundsValid() {
    return (max.x >= min.x &&
            max.y >= min.y &&
            max.z >= min.z);
    }

};

// 流出运算符 (输出) 的声明
std::ostream& operator<<(std::ostream& os, const SpatialBounds& bounds);

// 流入运算符 (输入) 的声明
std::istream& operator>>(std::istream& is, SpatialBounds& bounds);


struct Triangle {
    SpatialPoint p1, p2, p3;

    Triangle(const SpatialPoint& point1, const SpatialPoint& point2, const SpatialPoint& point3)
        : p1(point1), p2(point2), p3(point3) {}

    // Calculate the normal vector of the triangle plane (assuming a right-handed coordinate system)
    SpatialPoint normal() const {//获取法向量
        return ((p2 - p1).cross(p3 - p1)).normalize();
    }
};

bool isPointInTriangle(const Triangle& triangle, const SpatialPoint& point, float height);
bool isTriangleIntesectBound(const Triangle& triangle, const SpatialBounds& bound, float height);
bool isTriangleContainBound(const Triangle& triangle, const SpatialBounds& bound, float height);

struct Polygon {
    std::vector<Triangle> triangles;
    float height;
public:
    bool contains(const SpatialPoint& point) const{
        for(const Triangle &triangle:triangles){
            if(isPointInTriangle(triangle, point, height)){
                return true;
            }
        }
        return false;
    }
    bool intersects(const SpatialBounds& bound) const{
        for(const Triangle &triangle:triangles){
            if(isTriangleIntesectBound(triangle, bound, height)){
                return true;
            }
        }
        //这里还缺少一个判断就是这个矩形是一个细长的，每一个点都不在这个三棱锥内该怎么判断。
        return false;
    }
    // Generate a random triangle within the given bounds
    static Triangle generateRandomTriangle(const SpatialBounds& bounds) {
        // 随机生成三个点在给定的空间边界内
        SpatialPoint p1(
            bounds.min.x + static_cast<float>(rand()) / RAND_MAX * (bounds.max.x - bounds.min.x),
            bounds.min.y + static_cast<float>(rand()) / RAND_MAX * (bounds.max.y - bounds.min.y),
            bounds.min.z + static_cast<float>(rand()) / RAND_MAX * (bounds.max.z - bounds.min.z)
        );

        SpatialPoint p2(
            bounds.min.x + static_cast<float>(rand()) / RAND_MAX * (bounds.max.x - bounds.min.x),
            bounds.min.y + static_cast<float>(rand()) / RAND_MAX * (bounds.max.y - bounds.min.y),
            bounds.min.z + static_cast<float>(rand()) / RAND_MAX * (bounds.max.z - bounds.min.z)
        );

        SpatialPoint p3(
            bounds.min.x + static_cast<float>(rand()) / RAND_MAX * (bounds.max.x - bounds.min.x),
            bounds.min.y + static_cast<float>(rand()) / RAND_MAX * (bounds.max.y - bounds.min.y),
            bounds.min.z + static_cast<float>(rand()) / RAND_MAX * (bounds.max.z - bounds.min.z)
        );

        return Triangle(p1, p2, p3);
    }

    // Generate a new triangle that maintains the plane orientation of the previous triangle
    static Triangle generateNextTriangle(const SpatialBounds& bounds, const Triangle& previous) {
        // Get the normal of the previous triangle
        SpatialPoint normal = previous.normal();

        // Generate a random point within the bounds
        SpatialPoint randomPoint(
            bounds.min.x + static_cast<float>(rand()) / RAND_MAX * (bounds.max.x - bounds.min.x),
            bounds.min.y + static_cast<float>(rand()) / RAND_MAX * (bounds.max.y - bounds.min.y),
            bounds.min.z + static_cast<float>(rand()) / RAND_MAX * (bounds.max.z - bounds.min.z)
        );

        // Adjust the random point to lie on the same plane as the previous triangle
        //这个步骤是合理的，这个是计算新旧两个点在normal方向的投影值，保证两个投影相同，即共面。
        SpatialPoint p3 = randomPoint + normal * (previous.p1 - randomPoint).dot(normal);
        return Triangle(previous.p1, previous.p2, p3);
    }

    // Generate a mesh of triangles with the specified count within the given bounds
    static Polygon generateMesh(const SpatialBounds& bounds, int triangleCount,float height) {
        Polygon mesh;
        mesh.height = height;

        // Start with a random triangle
        Triangle first = generateRandomTriangle(bounds);
        mesh.triangles.push_back(first);

        // Generate subsequent triangles
        for (int i = 1; i < triangleCount; ++i) {
            Triangle next = generateNextTriangle(bounds, mesh.triangles.back());
            mesh.triangles.push_back(next);
        }

        return mesh;
    }

    // Calculate the bounding box for each triangle when extended along the normal's direction by `height`
    SpatialBounds getBounds() const {
        if (triangles.empty()) {
            return SpatialBounds(); // Return an empty bounding box if no triangles are present
        }

        SpatialBounds bounds;
        bool first = true;
        for (const Triangle& triangle : triangles) {
            // Original triangle vertices
            SpatialPoint p1 = triangle.p1;
            SpatialPoint p2 = triangle.p2;
            SpatialPoint p3 = triangle.p3;

            // Extend the vertices along the normal direction by the height
            SpatialPoint p1_ext = p1 + triangle.normal() * height;
            SpatialPoint p2_ext = p2 + triangle.normal() * height;
            SpatialPoint p3_ext = p3 + triangle.normal() * height;

            // Extend the bounding box to include the original and the extended vertices
            if(first){
                bounds.min = p1;
                bounds.max = p1;
                first = false;
            }else{
                bounds.updateBounds(p1);
            }
            bounds.updateBounds(p1);
            bounds.updateBounds(p2);
            bounds.updateBounds(p3);
            bounds.updateBounds(p1_ext);
            bounds.updateBounds(p2_ext);
            bounds.updateBounds(p3_ext);
        }

        return bounds;
    }
};

class MetricTree {
    public:
    MetricTreeNode* root;
    int id_count = 0;
    ThreadPoolWrapper thread_pool_wrapper;
    ~MetricTree(){
        deleteTree(root);
    }
    
    void deleteTree(MetricTreeNode* node) {
        if (node == nullptr) {
            return;
        }

        if (node->node_type == 0) { 
            // 处理叶子节点
            auto* leafNode = static_cast<MetricTreeLeafNode*>(node);
            delete leafNode;
        } 
        else if (node->node_type == 1) { 
            // 处理内部节点
            auto* innerNode = static_cast<MetricTreeInnerNode*>(node);

            if (innerNode->left != nullptr) {
                deleteTree(innerNode->left);
            }
            if (innerNode->right != nullptr) {
                deleteTree(innerNode->right);
            }

            delete innerNode;
        } 
        else {
            std::cerr << "Error: Unknown node type!" << std::endl;
        }
    }

    MetricTree(MetricTreeNode* root):root(root),thread_pool_wrapper(thread_pool_size){

    }
    void saveAllNodes(MetricTreeNode* node, std::unordered_map<int, MetricTreeNode*>& node_map) const {
        if (node == nullptr) {
            return;
        }

        if (node->node_type == 0) {
            MetricTreeLeafNode* leafNode = static_cast<MetricTreeLeafNode*>(node);
            node_map[leafNode->id] = leafNode;
        }
        else if (node->node_type == 1) {
            MetricTreeInnerNode* innerNode = static_cast<MetricTreeInnerNode*>(node);
            node_map[innerNode->id] = innerNode;

            if (innerNode->left != nullptr) {
                saveAllNodes(innerNode->left, node_map);
            }
            if (innerNode->right != nullptr) {
                saveAllNodes(innerNode->right, node_map);
            }
        }
        else {
            std::cerr << "Error: Unknown node type!" << std::endl;
        }
    }
    
    std::string serialize_to_string() const {
        return encodeMapToJSONString(serialize_to_map());
    }
    std::unordered_map<int, nlohmann::json> serialize_to_map() const {
        std::unordered_map<int, nlohmann::json> result;
        std::unordered_map<int, MetricTreeNode*> node_map;
        saveAllNodes(root, node_map);
        for (const auto& [id, node] : node_map) {
            result[id] = node->to_json();
        }
        return result;
    }

    static MetricTreeNode* deserialize_from_string(std::string data) {
        auto map_json = decodeJSONStringToMap(data);
        return deserialize_from_map(map_json);
    }
    static MetricTreeNode* deserialize_from_map(std::unordered_map<int, nlohmann::json> &data) {
        std::unordered_map<int, MetricTreeNode*> node_map;
        std::unordered_map<int, std::pair<int,int>> routes;
        for (const auto& [id, json] : data) {
            if(json["node_type"].get<int>()){//innernode
                node_map[id] = MetricTreeInnerNode::from_json(json);
                int left_id = json.at("left_id").get<int>();
                int right_id = json.at("right_id").get<int>();
                routes[id] = {left_id,right_id};
            }else{
                node_map[id] = MetricTreeLeafNode::from_json(json);
            }
        }
        std::queue<MetricTreeNode*> connecting_queue;
        connecting_queue.push(node_map[0]);//root
        while(!connecting_queue.empty()){
            auto *node = connecting_queue.front();
            connecting_queue.pop();
            if(node->node_type == 0){
                continue;
            }
            auto *inner_node = static_cast<MetricTreeInnerNode*>(node);
            std::pair<int,int> children = routes.at(inner_node->id);
            inner_node->left = node_map.at(children.first);
            inner_node->right = node_map.at(children.second);
            connecting_queue.push(inner_node->left);
            connecting_queue.push(inner_node->right);
        }
        return node_map[0];
    }

    bool checkTree(MetricTreeNode* node) {
        if (node == nullptr) {
            std::cout << "Error: Found an empty node!" << std::endl;
            return false;
        }

        if (MetricTreeLeafNode* leafNode = static_cast<MetricTreeLeafNode*>(node)) {
            if (leafNode->ids.empty()) {
                std::cout << "Error: Leaf node with empty ids!" << std::endl;
                return false;
            }
            return true;
        }

        if (MetricTreeInnerNode* innerNode = static_cast<MetricTreeInnerNode*>(node)) {
            if (innerNode->left == nullptr || innerNode->right == nullptr) {
                std::cout << "Error: Inner node with empty child!" << std::endl;
                return false;
            }

            bool leftValid = checkTree(innerNode->left);
            bool rightValid = checkTree(innerNode->right);

            return leftValid && rightValid;
        }

        std::cout << "Error: Unknown node type!" << std::endl;
        return false;
    }

    MetricTreeNode* buildTree(std::vector<int> &ids,int layer = 0) {
        std::cout <<"ids:"<<ids.size()<< std::endl;
        if(ids.size() < 10){
            MetricTreeLeafNode *leaf = new MetricTreeLeafNode();
            leaf->id = id_count++;
            leaf->ids = ids;
            return leaf;
        }
        // 选择一个中枢点作为分割点
        size_t middle_size = ids.size() / 2;
        Trajectory pivot = load_tracj(ids[middle_size]);

        {
            boost::asio::thread_pool thread_pool(100);
            std::vector<std::pair<int, float>> id_distances(ids.size());
            std::vector<std::pair<std::int64_t, std::int64_t>> task_assigned = split<std::int64_t>(0,ids.size(),100);

            for (size_t task_id = 0; task_id < task_assigned.size(); ++task_id) {
                boost::asio::post(thread_pool,[task_id, &id_distances,&task_assigned, &pivot,&ids]() {
                for (size_t i = task_assigned[task_id].first; i < task_assigned[task_id].second; ++i) {
                        id_distances[i] = {ids[i], trajectoryDistance(pivot, load_tracj(ids[i]))};
                    }
                });
            }
            thread_pool.join();
            std::nth_element(id_distances.begin(), id_distances.begin() + middle_size, id_distances.end(),
                            [](const std::pair<long long, double>& p1, const std::pair<long long, double>& p2) {
                                return p1.second < p2.second; // 根据距离排序
                            });

            // 步骤 3: 将排好序的 id 拷贝回原始 ids
            for (size_t i = 0; i < ids.size(); ++i) {
                ids[i] = id_distances[i].first;
            }
        }

        // 创建一个节点，存储分割信息
        MetricTreeInnerNode* inner = new MetricTreeInnerNode(pivot);
        inner->id = id_count++;
        inner->radius = (trajectoryDistance(pivot, load_tracj(ids[middle_size])) + trajectoryDistance(pivot, load_tracj(ids[middle_size-1])))/2;

        // 递归构建左右子树
        std::vector<int> left_ids(ids.begin(), ids.begin() + middle_size);
        std::vector<int> right_ids(ids.begin() + middle_size, ids.end());


        if(layer <= 2){
            auto left_future = std::async(std::launch::async, [this, &left_ids, layer]() {
                return this->buildTree(left_ids, layer + 1);
            });

            auto right_future = std::async(std::launch::async, [this, &right_ids, layer]() {
                return this->buildTree(right_ids, layer + 1);
            });
            inner->left = left_future.get();
            inner->right = right_future.get();
        }else{
            inner->left = buildTree(left_ids,layer+1);
            inner->right = buildTree(right_ids,layer+1);
        }
        return inner;
    }

public:
    // std::unordered_map<int,Trajectory> trajectories_source;
    MetricTree() :thread_pool_wrapper(100){
        std::vector<int> ids = TrajectoryManager::getAllTrajectoryIdsFromDatabase();
        root = buildTree(ids);
    }

    // 返回 k 近邻结果
    std::vector<std::pair<float,Trajectory>> kNearestNeighbors(Trajectory& query, int k, float approximate_ratio,float min_time,float max_time,double &db_time) const {
        if(approximate_ratio < 1){
            throw std::runtime_error("approximate_ratio mast be greater than 1"); 
        }
        std::vector<std::pair<float,Trajectory>> result;
        std::priority_queue<std::pair<float, MetricTreeNode*>,std::vector<std::pair<float, MetricTreeNode*>>,std::greater<std::pair<float, MetricTreeNode*>>> searchQueue;
        std::priority_queue<std::pair<float, int>> resultQueue;
       
        searchQueue.push({0,root});
        while(!searchQueue.empty())
        {
            if(resultQueue.size() >= k && searchQueue.top().first * approximate_ratio> resultQueue.top().first){
                break;
            }   
            auto *node = searchQueue.top().second;
            searchQueue.pop();
            if(node->node_type == 0)
            {
                MetricTreeLeafNode *leaf_node = static_cast<MetricTreeLeafNode*>(node);
                TimerClock tc;
                auto tracj_map = TrajectoryManager::loadTrajectoriesFromDatabase(leaf_node->ids);
                db_time += tc.milliSec();
                for(auto i:leaf_node->ids){
                    auto traj = tracj_map.at(i);
                    traj.cut_by_time(min_time,max_time);
                    resultQueue.push({trajectoryDistance(query,traj),i});
                    if(resultQueue.size() > k){
                        resultQueue.pop();
                    }
                }
            }else if(node->node_type == 1){
                MetricTreeInnerNode *inner_node = static_cast<MetricTreeInnerNode*>(node);
                auto distance_pair = inner_node->calculate_distance(query);
                searchQueue.push({distance_pair.first,static_cast<MetricTreeNode*>(inner_node->left)});
                searchQueue.push({distance_pair.second,static_cast<MetricTreeNode*>(inner_node->right)});
            }else{
                throw std::runtime_error("bad node type!");
            }
        }
        while (!resultQueue.empty()) {
            result.push_back({resultQueue.top().first, load_tracj(resultQueue.top().second)});
            resultQueue.pop();
        }
        std::reverse(result.begin(), result.end());
        return result;
    }
};


inline std::vector<std::pair<float, Trajectory>> trajectory_kNN_query_bruteforce(Trajectory &query_center,int k,float min_time,float max_time,std::unordered_map<int, Trajectory> &data){
    std::vector<std::pair<float, Trajectory>> dataVector;
    dataVector.reserve(data.size());
    for (auto &[id, trajectory] : data) {
        dataVector.push_back({0, trajectory});
    }

    std::vector<std::future<void>> futures;
    size_t chunk_size = dataVector.size() / thread_pool_size;
    
    for (int i = 0; i < thread_pool_size; ++i) {
        futures.push_back(std::async(std::launch::async, [&, i] {
            size_t start = i * chunk_size;
            size_t end = (i == thread_pool_size - 1) ? dataVector.size() : (i + 1) * chunk_size;
            for (size_t j = start; j < end; ++j) {
                dataVector[j].second.cut_by_time(min_time, max_time);
                dataVector[j].first = trajectoryDistance(query_center, dataVector[j].second);
            }
        }));
    }

    // 等待所有线程完成
    for (auto &fut : futures) {
        fut.get();
    }

    // Step 4: 根据距离排序所有结果
    std::sort(dataVector.begin(), dataVector.end(),
        [](const std::pair<float, Trajectory> &a, const std::pair<float, Trajectory> &b) {
            return a.first < b.first; 
        });
    dataVector.resize(k);

    return dataVector;
}

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

inline float pointToBoundsDistance(const SpatialPoint& point, const SpatialBounds& bounds) {
    float dx = max3(bounds.min.x - point.x, 0.0f, point.x - bounds.max.x);
    float dy = max3(bounds.min.y - point.y, 0.0f, point.y - bounds.max.y);
    float dz = max3(bounds.min.z - point.z, 0.0f, point.z - bounds.max.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline float pointToBoundsMaxDistance(const SpatialPoint& point, const SpatialBounds& bounds) {
    float dx = std::max(std::abs(point.x - bounds.min.x), std::abs(point.x - bounds.max.x));
    float dy = std::max(std::abs(point.y - bounds.min.y), std::abs(point.y - bounds.max.y));
    float dz = std::max(std::abs(point.z - bounds.min.z), std::abs(point.z - bounds.max.z));
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}


inline float pointToBoundsDistance(const SpatioTemporalData& point, const SpatialBounds& bounds) {
    float dx = max3(bounds.min.x - point.x, 0.0f, point.x - bounds.max.x);
    float dy = max3(bounds.min.y - point.y, 0.0f, point.y - bounds.max.y);
    float dz = max3(bounds.min.z - point.z, 0.0f, point.z - bounds.max.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}


inline float pointToBoundsDistance(const SpatialPoint& point, const Bounds& bounds) {
    float dx = max3(bounds.min.x - point.x, 0.0f, point.x - bounds.max.x);
    float dy = max3(bounds.min.y - point.y, 0.0f, point.y - bounds.max.y);
    float dz = max3(bounds.min.z - point.z, 0.0f, point.z - bounds.max.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline float pointToBoundsDistance(const SpatioTemporalData& point, const Bounds& bounds) {
    float dx = max3(bounds.min.x - point.x, 0.0f, point.x - bounds.max.x);
    float dy = max3(bounds.min.y - point.y, 0.0f, point.y - bounds.max.y);
    float dz = max3(bounds.min.z - point.z, 0.0f, point.z - bounds.max.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}


inline float calculateDTWDistance(const std::vector<SpatioTemporalData>& traj1, const std::vector<SpatioTemporalData>& traj2) {
    int m = traj1.size();
    int n = traj2.size();
    std::vector<std::vector<float>> dtw(m + 1, std::vector<float>(n + 1, std::numeric_limits<float>::infinity()));
    dtw[0][0] = 0;
    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            float cost = pointToPointDistance(traj1[i - 1], traj2[j - 1]);
            dtw[i][j] = cost + std::min({dtw[i - 1][j],      // 插入
                                         dtw[i][j - 1],      // 删除
                                         dtw[i - 1][j - 1]}); // 匹配
        }
    }
    return dtw[m][n];
}


inline float calculateDTWDistance(const Trajectory& traj1, const Trajectory& traj2) {
    return calculateDTWDistance(traj1.points,traj2.points);
}


inline float calculateCumulativeNearestNeighborDistance(const Trajectory& traj1, const Trajectory& traj2) {
    int m = traj1.points.size();
    int n = traj2.points.size();

    float totalDistance = 0.0;

    // 对轨迹1中的每个点进行处理
    for (int i = 0; i < m; ++i) {
        float minDistance = std::numeric_limits<float>::infinity();

        // 对轨迹2中的每个点计算距离，找到最小的距离
        for (int j = 0; j < n; ++j) {
            float dx = traj1.points[i].x - traj2.points[j].x;
            float dy = traj1.points[i].y - traj2.points[j].y;
            float distance = std::sqrt(dx * dx + dy * dy);

            if (distance < minDistance) {
                minDistance = distance;
            }
        }

        totalDistance += minDistance;  // 累加最近距离
    }

    // 计算并返回平均距离
    return totalDistance / m;
}

float calculateAverageDistance(const Trajectory& traj1, const Trajectory& traj2);

#endif // SPATIOTEMPORAL_DATA_H 