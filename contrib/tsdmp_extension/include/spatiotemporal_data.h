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
#include "parameter.h"

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

    static SpatioTemporalData from_binary(const std::string& binary) {
        if (binary.size() != sizeof(SpatioTemporalData)) {
            throw std::runtime_error("Binary data size mismatch");
        }
        SpatioTemporalData result;
        std::memcpy(&result, binary.data(), sizeof(SpatioTemporalData));
        return result;
    }

    bool match(std::uint8_t type){
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

    // 必须实现 operator< 用于 map
    bool operator<(const SpatioTemporalData& other) const {
        if (tid < other.tid) {return true;}
        if (foreign_key < other.foreign_key) {return true;}
        if (fid < other.fid) {return true;}
        if (pid < other.pid) {return true;}
        return false;
    }

    void print_key_elements() const {
        std::cout << "tid: " << tid << std::endl;
        std::cout << "fid: " << fid << std::endl;
        std::cout << "foreign_key: " << foreign_key << std::endl;
        std::cout << "pid: " << pid << std::endl;
    }

    std::string toString() const {
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

    SpatioTemporalData() : SpatialPoint(),pid(0), fid(0),time(0) {}

    SpatioTemporalData(float x, float y, float z) : SpatialPoint(x, y, z), pid(0), fid(0),time(0) {}
    SpatioTemporalData(float x, float y, float z,float time) : SpatialPoint(x, y, z),time(time), pid(0), fid(0) {}

    void print();

    friend void swap(SpatioTemporalData& a, SpatioTemporalData& b) noexcept;


private:
    uint32_t normalizeCoordinate(float coord, float minCoord, float maxCoord) const;
};

namespace std {
    template<>
    struct hash<SpatioTemporalData> {
        size_t operator()(const SpatioTemporalData& obj) const {
            return hash<std::int64_t>()(obj.tid) ^ hash<std::int64_t>()(obj.fid) && hash<std::int64_t>()(obj.foreign_key) ^ hash<std::int64_t>()(obj.pid);
        }
    };
}

#endif // SPATIOTEMPORAL_DATA_H 