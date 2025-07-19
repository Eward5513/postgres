/**
 * @file spatiotemporal_data.h
 * @brief Core data structures and utility functions for spatiotemporal data processing
 * 
 * This file contains the fundamental data structures for handling spatiotemporal data,
 * including spatial points, bounds, trajectories, point clouds, and meshes. It provides
 * efficient storage, manipulation, and query operations for 3D spatial data with temporal
 * dimensions.
 * 
 * Key components:
 * - SpatialPoint: Basic 3D point with vector operations
 * - SpatioTemporalData: Extended spatial point with temporal and metadata
 * - SpatialBounds/Bounds: Bounding box representations for spatial regions
 * - Trajectory/PointCloud/Mesh: Data collection classes
 * - Distance and indexing utility functions
 * 
 * @author Zhang Teng
 * @date 2024
 */

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

// Forward declarations to avoid circular dependencies
struct SpatialBounds;
struct Bounds;
class Trajectory;
class Mesh;
class PointCloud;

// ============================================================================
// Basic Spatial Types
// ============================================================================

/**
 * @brief Basic 3D spatial point with vector arithmetic operations
 * 
 * Represents a point in 3D space with x, y, z coordinates. Provides
 * essential vector operations like addition, subtraction, scaling,
 * dot product, cross product, and normalization.
 */
struct SpatialPoint {
    float x, y, z;  ///< 3D coordinates

    // Constructors
    SpatialPoint() : x(0), y(0), z(0) {}
    SpatialPoint(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    // Vector arithmetic operations
    SpatialPoint operator+(const SpatialPoint& other) const {
        return {x + other.x, y + other.y, z + other.z};
    }

    SpatialPoint operator-(const SpatialPoint& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }

    SpatialPoint operator*(float scalar) const {
        return {x * scalar, y * scalar, z * scalar};
    }

    /**
     * @brief Compute dot product with another point
     * @param other The other point
     * @return Dot product result
     */
    float dot(const SpatialPoint& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    /**
     * @brief Compute cross product with another point
     * @param other The other point
     * @return Cross product result as a new point
     */
    SpatialPoint cross(const SpatialPoint& other) const {
        return {
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        };
    }

    /**
     * @brief Calculate the Euclidean length/magnitude of the vector
     * @return Length of the vector
     */
    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    /**
     * @brief Get normalized unit vector
     * @return Normalized vector with length 1
     */
    SpatialPoint normalize() const {
        float len = length();
        return {x / len, y / len, z / len};
    }
};

// ============================================================================
// Data Type Enums and Operators
// ============================================================================

/**
 * @brief Data type classification for different kinds of spatial data
 * 
 * Uses bit flags to allow combining multiple types. Each data type
 * corresponds to different application scenarios:
 * - PointCloudPoint: LiDAR/sensor data with intensity
 * - MeshPoint: 3D model vertices with color information
 * - TrajectoryPoint: Moving object positions with speed data
 */
enum DataType : uint8_t {
    PointCloudPoint = 0b1,      ///< Point cloud data (intensity)
    MeshPoint = 0b10,           ///< Mesh vertex data (color RGB)
    TrajectoryPoint = 0b100     ///< Trajectory point data (speed)
};

// Bitwise operators for DataType enum
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
// SpatioTemporalData - Core Data Structure
// ============================================================================

/**
 * @brief Extended spatial point with temporal dimension and metadata
 * 
 * This is the core data structure that extends SpatialPoint with:
 * - Temporal information (time field)
 * - Data type classification and type-specific data
 * - Unique identification (file ID, point ID, foreign key)
 * - User information and deletion status
 * 
 * The union DataUnion stores type-specific data efficiently:
 * - PointCloud: intensity value
 * - Trajectory: speed value  
 * - Mesh: RGB color components
 */
struct SpatioTemporalData : public SpatialPoint {
    /**
     * @brief Union for storing type-specific data efficiently
     */
    union DataUnion {
        struct {
            float intensity;        ///< Intensity value for point cloud data
        } PointCloud;
        struct {
            float speed;           ///< Speed value for trajectory data
        } Trajectoy;               ///< Note: typo in original code, keeping for compatibility
        struct {
            uint8_t colorR;        ///< Red color component for mesh data
            uint8_t colorG;        ///< Green color component for mesh data
            uint8_t colorB;        ///< Blue color component for mesh data
        } Mesh;
    };

    // Core identification and metadata fields
    DataType tid;                       ///< Data type identifier
    file_id_t fid;                     ///< File identifier
    point_id_t pid;                    ///< Point identifier within file
    foreign_key_id_t foreign_key;      ///< Foreign key for relational mapping
    user_id_t user_id = -1;           ///< User identifier (default: unassigned)
    float time;                        ///< Temporal coordinate
    DataUnion external_data;           ///< Type-specific data storage
    bool is_deleted = false;           ///< Soft deletion flag

    // Constructors
    SpatioTemporalData() : SpatialPoint(), pid(0), fid(0), time(0) {}
    SpatioTemporalData(float x, float y, float z) : SpatialPoint(x, y, z), pid(0), fid(0), time(0) {}
    SpatioTemporalData(float x, float y, float z, float time) : SpatialPoint(x, y, z), time(time), pid(0), fid(0) {}

    /**
     * @brief Serialize object to binary string for storage/transmission
     * @return Binary representation as string
     */
    std::string to_binary() const {
        return std::string(reinterpret_cast<const char*>(this), sizeof(*this));
    }

    /**
     * @brief Check if data matches specified type mask
     * @param type Type mask to match against
     * @return True if type matches
     */
    bool match(std::uint8_t type) const {
        return type & tid;
    }

    /**
     * @brief Generate unique 64-bit identifier from component IDs
     * @return Unique identifier combining all ID fields
     */
    uint64_t unique_id() const {
        uint64_t result = 0;
        result |= static_cast<uint64_t>(foreign_key) << 48;
        result |= static_cast<uint64_t>(tid) << (32+13);
        result |= static_cast<uint64_t>(fid) << 32;
        result |= static_cast<uint64_t>(pid) << 0;
        return result;
    }

    /**
     * @brief Equality comparison based on all ID fields
     */
    bool operator==(const SpatioTemporalData& other) const {
        return foreign_key == other.foreign_key && tid == other.tid && fid == other.fid && pid == other.pid;
    }

    /**
     * @brief Less-than comparison for sorting/ordering
     */
    bool operator<(const SpatioTemporalData& other) const {
        if (tid < other.tid) return true;
        if (foreign_key < other.foreign_key) return true;
        if (fid < other.fid) return true;
        if (pid < other.pid) return true;
        return false;
    }

    // Complex functions - declarations only (implemented in .cpp)
    static SpatioTemporalData from_binary(const std::string& binary);
    void print_key_elements() const;
    std::string toString() const;
    void print() const;
    
    friend void swap(SpatioTemporalData& a, SpatioTemporalData& b) noexcept;

private:
    /**
     * @brief Normalize coordinate to 32-bit range for indexing
     * @param coord Coordinate value to normalize
     * @param minCoord Minimum coordinate in range
     * @param maxCoord Maximum coordinate in range
     * @return Normalized 32-bit coordinate
     */
    uint32_t normalizeCoordinate(float coord, float minCoord, float maxCoord) const;
};

// Hash specialization for SpatioTemporalData to use in unordered containers
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
// Spatial Bounds - 3D Bounding Box Operations
// ============================================================================

/**
 * @brief 3D spatial bounding box for spatial queries and indexing
 * 
 * Represents an axis-aligned bounding box (AABB) in 3D space.
 * Provides operations for:
 * - Spatial containment testing
 * - Intersection detection  
 * - Bounds expansion and union operations
 * - Vertex enumeration
 */
struct SpatialBounds {
    SpatialPoint min;   ///< Minimum corner of bounding box
    SpatialPoint max;   ///< Maximum corner of bounding box
    
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
    
    /**
     * @brief Update bounds to include a new point
     * @param point Point to include in bounds
     */
     void updateBounds(const SpatialPoint& point) {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }
    
    /**
     * @brief Compute intersection of two bounding boxes
     * @param other Other bounding box
     * @return Intersection bounds
     */
    SpatialBounds operator&(const SpatialBounds& other) const{
        SpatialBounds result = *this;
        result.min.x = std::max(min.x, other.min.x);
        result.min.y = std::max(min.y, other.min.y);
        result.min.z = std::max(min.z, other.min.z);
        result.max.x = std::min(max.x, other.max.x);
        result.max.y = std::min(max.y, other.max.y);
        result.max.z = std::min(max.z, other.max.z);
        return result;
    }
    
    /**
     * @brief Compute union of two bounding boxes
     * @param other Other bounding box
     * @return Union bounds
     */
    SpatialBounds operator|(const SpatialBounds& other) const{
        SpatialBounds result = *this;
        result.min.x = std::min(min.x, other.min.x);
        result.min.y = std::min(min.y, other.min.y);
        result.min.z = std::min(min.z, other.min.z);
        result.max.x = std::max(max.x, other.max.x);
        result.max.y = std::max(max.y, other.max.y);
        result.max.z = std::max(max.z, other.max.z);
        return result;
    }
    
    /**
     * @brief Expand bounds to include a point
     * @param point Point to include
     * @return New bounds including the point
     */
    SpatialBounds operator|(const SpatialPoint& point) const{
        SpatialBounds result = *this;
        result.min.x = std::min(min.x, point.x);
        result.min.y = std::min(min.y, point.y);
        result.min.z = std::min(min.z, point.z);
        result.max.x = std::max(max.x, point.x);
        result.max.y = std::max(max.y, point.y);
        result.max.z = std::max(max.z, point.z);
        return result;
    }

    /**
     * @brief Get geometric center of the bounding box
     * @return Center point
     */
    SpatialPoint getCenter() const {
        return SpatialPoint(
            (min.x + max.x) / 2.0f,
            (min.y + max.y) / 2.0f,
            (min.z + max.z) / 2.0f
        );
    }

    /**
     * @brief Test if bounds contain a point
     * @param point Point to test
     * @return True if point is inside bounds
     */
    bool contains(const SpatialPoint& point) const {
        return (point.x >= min.x && point.x <= max.x) &&
               (point.y >= min.y && point.y <= max.y) &&
               (point.z >= min.z && point.z <= max.z);
    }

    /**
     * @brief Test if bounds completely contain another bounds
     * @param other Other bounds to test
     * @return True if other bounds is completely inside this bounds
     */
    bool contains(const SpatialBounds& other) const {
        return (other.min.x >= min.x && other.max.x <= max.x) &&
               (other.min.y >= min.y && other.max.y <= max.y) &&
               (other.min.z >= min.z && other.max.z <= max.z);
    }

    /**
     * @brief Test if bounds intersect with another bounds
     * @param other Other bounds to test
     * @return True if bounds intersect
     */
    bool intersects(const SpatialBounds& other) const {
        return !(max.x < other.min.x || min.x > other.max.x ||
                 max.y < other.min.y || min.y > other.max.y ||
                 max.z < other.min.z || min.z > other.max.z);
    }

    /**
     * @brief Validate bounds (max >= min for all dimensions)
     * @return True if bounds are valid
     */
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
// Bounds - Spatiotemporal Bounding Box
// ============================================================================

/**
 * @brief 4D spatiotemporal bounding box (3D space + time dimension)
 * 
 * Extended version of SpatialBounds that includes temporal dimension.
 * Used for spatiotemporal indexing, queries, and data structure operations.
 * Supports octree-style spatial subdivision and temporal filtering.
 */
struct Bounds {
    SpatioTemporalData min;   ///< Minimum corner (including time)
    SpatioTemporalData max;   ///< Maximum corner (including time)
    bool first_flag;                ///< Internal state flag

    // Constructors
    Bounds() : min(SpatioTemporalData()), max(SpatioTemporalData()), first_flag(true) {}
    Bounds(const SpatioTemporalData& minimum, const SpatioTemporalData& maximum) : min(minimum), max(maximum), first_flag(false) {}
    Bounds(SpatioTemporalData& p) : min(p), max(p), first_flag(false) {}

    /**
     * @brief Validate spatiotemporal bounds
     * @return True if bounds are valid (max >= min for all dimensions)
     */
    bool isBoundsValid() const {
        return (max.x >= min.x && max.y >= min.y && max.z >= min.z);
    }

    /**
     * @brief Test if this bounds completely contains another bounds
     * @param other Other bounds to test
     * @return True if other bounds is completely inside this bounds
     */
    bool contains(const Bounds& other) const {
        return (min.x <= other.min.x && max.x >= other.max.x &&
                min.y <= other.min.y && max.y >= other.max.y &&
                min.z <= other.min.z && max.z >= other.max.z);
    }

    /**
     * @brief Convert to spatial bounds (dropping time dimension)
     * @return Equivalent SpatialBounds
     */
    SpatialBounds to_spatial_bound() const {
        SpatialBounds result;
        result.min = min;
        result.max = max;
        return result;
    }

    /**
     * @brief Update bounds from ikd-tree min/max arrays
     * @param min_value Array of minimum values [x, y, z]
     * @param max_value Array of maximum values [x, y, z]
     */
    void updateViaIkdtree(const float min_value[3], const float max_value[3]) {
        min.x = min_value[0];
        min.y = min_value[1];
        min.z = min_value[2];
        max.x = max_value[0];
        max.y = max_value[1];
        max.z = max_value[2];
    }

    /**
     * @brief Get geometric center of the spatiotemporal bounds
     * @return Center point as SpatioTemporalData
     */
    SpatioTemporalData getCenter() const {
        return SpatioTemporalData(
            (min.x + max.x) / 2.0f,
            (min.y + max.y) / 2.0f,
            (min.z + max.z) / 2.0f
        );
    }

    /**
     * @brief Get size of largest dimension
     * @return Maximum extent across all spatial dimensions
     */
    float getSize() const {
        float maxX = max.x - min.x;
        float maxY = max.y - min.y;
        float maxZ = max.z - min.z;
        return std::max(std::max(maxX, maxY), maxZ);
    }

    /**
     * @brief Test intersection with another bounds (inclusive)
     * @param other Other bounds to test
     * @return True if bounds intersect
     */
    bool intersects(const Bounds& other) const {
        return (min.x <= other.max.x && max.x >= other.min.x) &&
               (min.y <= other.max.y && max.y >= other.min.y) &&
               (min.z <= other.max.z && max.z >= other.min.z);
    }

    /**
     * @brief Test intersection with another bounds (exclusive max)
     * @param other Other bounds to test
     * @return True if bounds intersect (exclusive boundary)
     */
    bool intersects2(const Bounds& other) const {
        return (min.x <= other.max.x && max.x > other.min.x) &&
               (min.y <= other.max.y && max.y > other.min.y) &&
               (min.z <= other.max.z && max.z > other.min.z);
    }

    /**
     * @brief Equality comparison
     */
    bool operator==(const Bounds& other) const {
        return (min.x == other.min.x && min.y == other.min.y && min.z == other.min.z &&
                max.x == other.max.x && max.y == other.max.y && max.z == other.max.z);
    }

    /**
     * @brief Inequality comparison
     */
    bool operator!=(const Bounds& other) const {
        return !(other == *this);
    }

    // Bounds combination operators
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

    /**
     * @brief Test if bounds contain a point
     * @param point Point to test
     * @return True if point is inside bounds
     */
    bool contains(const SpatioTemporalData& point) const {
        return (point.x >= min.x && point.x <= max.x) &&
               (point.y >= min.y && point.y <= max.y) &&
               (point.z >= min.z && point.z <= max.z);
    }

    /**
     * @brief Test if bounds contain coordinates (with maximum bounds check)
     * @param x X coordinate
     * @param y Y coordinate  
     * @param z Z coordinate
     * @param maximum Maximum bounds for boundary condition handling
     * @return True if coordinates are inside bounds
     */
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
    void update(const SpatioTemporalData& point);
    // void update(const void* json_data);  // Unused function - commented out
    bool contains2(const float x, const float y, const float z, const Bounds& maximum) const;
    void print() const;
    
    // Friend function declarations
    friend std::istream& operator>>(std::istream& is, Bounds& bounds);
    friend std::ostream& operator<<(std::ostream& os, const Bounds& bounds);
};

// ============================================================================
// Geometric Types - Triangles and Polygons
// ============================================================================

/**
 * @brief 3D triangle defined by three spatial points
 * 
 * Basic geometric primitive for mesh representations and
 * spatial containment testing. Provides normal vector calculation.
 */
struct Triangle {
    SpatialPoint p1, p2, p3;   ///< Triangle vertices

    Triangle(const SpatialPoint& point1, const SpatialPoint& point2, const SpatialPoint& point3)
        : p1(point1), p2(point2), p3(point3) {}

    /**
     * @brief Calculate triangle normal vector
     * @return Normalized normal vector
     */
    SpatialPoint normal() const {
        return ((p2 - p1).cross(p3 - p1)).normalize();
    }
};

/**
 * @brief Collection of triangles forming a 3D polygon/mesh
 * 
 * Represents complex 3D shapes using triangular facets.
 * Supports spatial queries like containment and intersection testing.
 */
struct Polygon {
    std::vector<Triangle> triangles;   ///< Component triangles
    float height;                      ///< Extrusion height for volume

    /**
     * @brief Test if point is contained within polygon
     * @param point Point to test
     * @return True if point is inside polygon
     */
    bool contains(const SpatialPoint& point) const;
    
    /**
     * @brief Test if polygon intersects with bounding box
     * @param bound Bounding box to test
     * @return True if intersection exists
     */
    bool intersects(const SpatialBounds& bound) const;
    
    /**
     * @brief Get bounding box of entire polygon
     * @return Minimal bounding box containing all triangles
     */
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
// Data Collection Classes - Structured Data Containers
// ============================================================================

/**
 * @brief Time-ordered sequence of spatiotemporal points representing movement
 * 
 * Stores trajectory data with temporal ordering and provides operations for:
 * - JSON serialization/deserialization
 * - Temporal sorting and filtering
 * - Trajectory analysis and comparison
 */
class Trajectory {
public:
    int id;                                        ///< Trajectory identifier
    std::vector<SpatioTemporalData> points;       ///< Ordered sequence of points

    // Complex functions - declarations only
    nlohmann::json to_json() const;
    static Trajectory from_json(const nlohmann::json& json_obj);
    void sort_by_timestamp();
    void cut_by_time(float min_time, float max_time);
};

/**
 * @brief 3D mesh representation with vertices and connectivity
 * 
 * Stores mesh data including vertex positions and topological connections
 * between vertices defining faces/surfaces.
 */
class Mesh {
public:
    int id;                                        ///< Mesh identifier
    std::vector<SpatioTemporalData> points;       ///< Mesh vertices
    std::vector<std::vector<int>> connections;    ///< Vertex connectivity (faces)
};

/**
 * @brief Unstructured collection of 3D points
 * 
 * Represents point cloud data from sensors like LiDAR or structured light scanners.
 * Each point can contain additional attributes like intensity or color.
 */
class PointCloud {
public:
    int id;                                        ///< Point cloud identifier
    std::vector<SpatioTemporalData> points;       ///< Point collection
};

/**
 * @brief Interleave bits of 3D coordinates for Z-order curve indexing
 * 
 * Performs bit interleaving (Morton encoding) to convert 3D spatial coordinates
 * into a single 1D index that preserves spatial locality. This is essential
 * for spatial data structures like octrees and efficient spatial queries.
 * 
 * The algorithm interleaves bits in the pattern: z2,y2,x2,z1,y1,x1,z0,y0,x0
 * where subscripts represent bit positions from LSB to MSB.
 * 
 * @param x X-coordinate (spatial dimension)
 * @param y Y-coordinate (spatial dimension)
 * @param z Z-coordinate (spatial dimension)
 * @param level Number of bits to process from each coordinate
 * @return uint64_t Morton-encoded index preserving spatial locality
 * 
 * @note Level parameter determines precision: level=10 gives 30-bit index
 * @note Essential for octree construction and spatial partitioning
 * @note Thread-safe as it performs only arithmetic operations
 * 
 * @example
 * uint64_t morton = interleaveBits(5, 3, 7, 4);  // 4-bit precision
 * // Converts (x=5, y=3, z=7) to single index for spatial lookup
 */
 uint64_t interleaveBits(uint64_t x, uint64_t y, uint64_t z, uint64_t level);

// ============================================================================
// Utility Functions - Indexing and Distance Calculations
// ============================================================================

/**
 * @brief Calculate Morton/Z-order index for 3D point
 * 
 * Computes space-filling curve index for efficient spatial indexing.
 * Used in octree and spatial hash table implementations.
 * 
 * @param x X coordinate
 * @param y Y coordinate  
 * @param z Z coordinate
 * @param bound Bounding region for normalization
 * @param level Subdivision level/precision
 * @return 64-bit Morton index
 */
uint64_t indexOfPoint(float x, float y, float z, SpatialBounds bound, int level);

// ============================================================================
// Distance Functions - Spatial Proximity Calculations
// ============================================================================

/**
 * @brief Calculate Euclidean distance between two spatial points
 * @param point1 First point
 * @param point2 Second point
 * @return Euclidean distance
 */
inline float pointToPointDistance(const SpatialPoint& point1, const SpatialPoint& point2) {
    float dx = point1.x - point2.x;
    float dy = point1.y - point2.y;
    float dz = point1.z - point2.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/**
 * @brief Calculate Euclidean distance between two spatiotemporal points
 * @param point1 First point
 * @param point2 Second point
 * @return Euclidean distance (spatial only)
 */
inline float pointToPointDistance(const SpatioTemporalData& point1, const SpatioTemporalData& point2) {
    float dx = point1.x - point2.x;
    float dy = point1.y - point2.y;
    float dz = point1.z - point2.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Point-to-bounds distance functions (minimum distance to boundary)
float pointToBoundsDistance(const SpatialPoint& point, const SpatialBounds& bounds);
float pointToBoundsMaxDistance(const SpatialPoint& point, const SpatialBounds& bounds);
float pointToBoundsDistance(const SpatioTemporalData& point, const SpatialBounds& bounds);
float pointToBoundsDistance(const SpatialPoint& point, const Bounds& bounds);
float pointToBoundsDistance(const SpatioTemporalData& point, const Bounds& bounds);

// ============================================================================
// Trajectory Distance Functions - Time Series Analysis
// ============================================================================

/**
 * @brief Calculate Dynamic Time Warping distance between trajectories
 * 
 * DTW allows optimal alignment of trajectory points with different temporal
 * sampling rates, providing robust similarity measurement.
 * 
 * @param traj1 First trajectory point sequence
 * @param traj2 Second trajectory point sequence
 * @return DTW distance value
 */
float calculateDTWDistance(const std::vector<SpatioTemporalData>& traj1, const std::vector<SpatioTemporalData>& traj2);

/**
 * @brief Calculate DTW distance between trajectory objects
 * @param traj1 First trajectory
 * @param traj2 Second trajectory
 * @return DTW distance value
 */
float calculateDTWDistance(const Trajectory& traj1, const Trajectory& traj2);

/**
 * @brief Calculate cumulative nearest neighbor distance
 * 
 * For each point in first trajectory, find nearest point in second trajectory
 * and average the distances. Provides asymmetric similarity measure.
 * 
 * @param traj1 First trajectory
 * @param traj2 Second trajectory
 * @return Average nearest neighbor distance
 */
float calculateCumulativeNearestNeighborDistance(const Trajectory& traj1, const Trajectory& traj2);

/**
 * @brief Calculate average distance between all point pairs
 * 
 * Computes mean distance between all pairs of points from two trajectories.
 * Simple but computationally expensive similarity measure.
 * 
 * @param traj1 First trajectory
 * @param traj2 Second trajectory
 * @return Average pairwise distance
 */
float calculateAverageDistance(const Trajectory& traj1, const Trajectory& traj2);

// ============================================================================
// Query Functions - Spatial and Temporal Search
// ============================================================================

/**
 * @brief Brute-force k-nearest neighbor search for trajectories
 * 
 * Exhaustive search through all trajectories to find k most similar ones
 * to the query trajectory within specified time range. Uses DTW distance
 * for similarity measurement with parallel processing for efficiency.
 * 
 * @param query_center Query trajectory to match against
 * @param k Number of nearest neighbors to return
 * @param min_time Minimum time bound for filtering
 * @param max_time Maximum time bound for filtering
 * @param data Collection of trajectories to search
 * @return Vector of k nearest trajectories with distances
 */
std::vector<std::pair<float, Trajectory>> trajectory_kNN_query_bruteforce(
    Trajectory& query_center, int k, float min_time, float max_time, 
    std::unordered_map<int, Trajectory>& data);

#endif // SPATIOTEMPORAL_DATA_H 