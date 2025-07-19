/**
 * @file spatiotemporal_data.cpp
 * @brief Implementation of spatiotemporal data structures and utility functions
 * 
 * This file provides the implementation for all classes and functions declared
 * in spatiotemporal_data.h. It includes:
 * - Core data structure operations (serialization, comparison, etc.)
 * - Spatial and spatiotemporal bounds calculations
 * - Geometric operations for triangles and polygons
 * - Distance calculations and similarity measures
 * - Trajectory processing and analysis functions
 * - Spatial indexing and query operations
 * 
 * The functions are organized by dependency order to ensure proper compilation
 * and logical flow from basic operations to complex algorithms.
 * 
 * @author Zhang Teng
 * @date 2024
 */

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

// ============================================================================
// SpatioTemporalData Core Operations
// ============================================================================

/**
 * @brief Deserialize SpatioTemporalData from binary string
 * 
 * Reconstructs a SpatioTemporalData object from its binary representation.
 * Validates the binary data size to ensure data integrity.
 * 
 * @param binary Binary string containing serialized data
 * @return Deserialized SpatioTemporalData object
 * @throws std::runtime_error if binary size doesn't match expected size
 */
SpatioTemporalData SpatioTemporalData::from_binary(const std::string& binary) {
    if (binary.size() != sizeof(SpatioTemporalData)) {
        throw std::runtime_error("Binary data size mismatch");
    }
    SpatioTemporalData result;
    std::memcpy(&result, binary.data(), sizeof(SpatioTemporalData));
    return result;
}

/**
 * @brief Print key identification elements for debugging
 * 
 * Outputs the core identification fields (tid, fid, foreign_key, pid)
 * to standard output for debugging and verification purposes.
 */
void SpatioTemporalData::print_key_elements() const {
    std::cout << "tid: " << tid << std::endl;
    std::cout << "fid: " << fid << std::endl;
    std::cout << "foreign_key: " << foreign_key << std::endl;
    std::cout << "pid: " << pid << std::endl;
}

/**
 * @brief Convert SpatioTemporalData to formatted string representation
 * 
 * Creates a comma-separated string representation suitable for database
 * storage or text export. Includes all spatial, temporal, and metadata fields
 * with proper formatting for different data types.
 * 
 * Format: POINT(x y z),time,tid,fid,pid,foreign_key,userid,intensity,speed,r,g,b
 * 
 * @return Formatted string representation
 */
std::string SpatioTemporalData::toString() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6); // Set floating point precision to 6 digits

    // Spatial coordinates in POINT format
    oss << "POINT(" << x << " " << y << " " << z << ")";
    // Temporal coordinate
    oss << "," << time;
    // Data type identifier
    oss << "," << static_cast<int>(tid);
    // File and point identifiers
    oss << "," << fid;
    oss << "," << pid;
    // Relational mapping
    oss << "," << foreign_key;
    // User identifier
    oss << "," << user_id;
    // Type-specific data with conditional extraction
    oss << "," << (tid == DataType::PointCloudPoint ? external_data.PointCloud.intensity : 0.0f);
    oss << "," << (tid == DataType::TrajectoryPoint ? external_data.Trajectoy.speed : 0.0f);
    oss << "," << (tid == DataType::MeshPoint ? static_cast<int>(external_data.Mesh.colorR) : 0);
    oss << "," << (tid == DataType::MeshPoint ? static_cast<int>(external_data.Mesh.colorG) : 0);
    oss << "," << (tid == DataType::MeshPoint ? static_cast<int>(external_data.Mesh.colorB) : 0);

    return oss.str();
}

/**
 * @brief Print complete SpatioTemporalData information
 * 
 * Outputs all fields of the data structure in human-readable format
 * for debugging and analysis purposes.
 */
void SpatioTemporalData::print() const {
    std::cout << "SpatioTemporalData: (" << x << ", " << y << ", " << z 
              << "), time: " << time << ", tid: " << static_cast<int>(tid) 
              << ", fid: " << fid << ", pid: " << pid 
              << ", foreign_key: " << foreign_key << std::endl;
}

/**
 * @brief Efficient swap operation for SpatioTemporalData objects
 * 
 * Performs member-wise swap of all fields using std::swap for optimal
 * performance. Used in sorting and container operations.
 * 
 * @param a First object to swap
 * @param b Second object to swap
 */
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

/**
 * @brief Normalize coordinate to 32-bit integer range for spatial indexing
 * 
 * Maps a floating-point coordinate within [minCoord, maxCoord] to the full
 * range of a 32-bit unsigned integer. Used for spatial hashing and indexing.
 * 
 * @param coord Coordinate value to normalize
 * @param minCoord Minimum coordinate in the valid range
 * @param maxCoord Maximum coordinate in the valid range
 * @return Normalized coordinate in 32-bit integer space
 */
uint32_t SpatioTemporalData::normalizeCoordinate(float coord, float minCoord, float maxCoord) const {
    const uint32_t maxRange = 0xFFFFFFFF; // 2^32 - 1
    return static_cast<uint32_t>((coord - minCoord) / (maxCoord - minCoord) * maxRange);
}

// ============================================================================
// Bounds Utility Functions
// ============================================================================

/**
 * @brief Create bounds around center point with specified dimensions
 * 
 * Constructs a spatiotemporal bounding box centered at the given point
 * with the specified width, height, and depth extents.
 * 
 * @param center Center point of the bounds
 * @param width Width extent (X dimension)
 * @param height Height extent (Y dimension)  
 * @param depth Depth extent (Z dimension)
 * @return Constructed Bounds object
 */
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

/**
 * @brief Generate random point within bounds
 * 
 * Creates a random spatiotemporal point within the bounds using uniform
 * distribution across all spatial and temporal dimensions.
 * 
 * @param type Data type to assign to the generated point
 * @return Random SpatioTemporalData point within bounds
 */
SpatioTemporalData Bounds::generateRandomPoint(DataType type) const {
    SpatioTemporalData randomPoint;
    randomPoint.tid = type;
    randomPoint.x = min.x + static_cast<float>(rand()) / RAND_MAX * (max.x - min.x);
    randomPoint.y = min.y + static_cast<float>(rand()) / RAND_MAX * (max.y - min.y);
    randomPoint.z = min.z + static_cast<float>(rand()) / RAND_MAX * (max.z - min.z);
    randomPoint.time = min.time + static_cast<float>(rand()) / RAND_MAX * (max.time - min.time);
    return randomPoint;
}

/**
 * @brief Find axis with largest spatial extent
 * 
 * Determines which spatial dimension (X, Y, or Z) has the largest range.
 * Used for adaptive spatial subdivision algorithms like kd-trees.
 * 
 * @return Axis index: 0 for X, 1 for Y, 2 for Z
 */
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

/**
 * @brief Create bounds covering maximum possible space
 * 
 * Generates bounds that span the full range of floating-point values,
 * useful as initial bounds for algorithms that progressively refine.
 * 
 * @return Maximum possible bounds
 */
Bounds Bounds::limit_max() {
    Bounds result;
    result.min.x = result.min.y = result.min.z = -std::numeric_limits<float>::max();
    result.max.x = result.max.y = result.max.z = std::numeric_limits<float>::max();
    return result;
}

/**
 * @brief Expand bounds by multiplication factor
 * 
 * Increases the size of bounds by the specified factor while maintaining
 * the same center point. A factor of 2.0 doubles the size in all dimensions.
 * 
 * @param factor Multiplication factor for expansion (>1.0 for expansion)
 */
void Bounds::expand(double factor) {
    // Calculate original dimensions
    float width = max.x - min.x;
    float height = max.y - min.y;
    float depth = max.z - min.z;

    // Calculate half of new dimensions
    float newWidth = width * factor / 2.0f;
    float newHeight = height * factor / 2.0f;
    float newDepth = depth * factor / 2.0f;

    // Update bounds symmetrically around center
    min.x = min.x - newWidth;
    min.y = min.y - newHeight;
    min.z = min.z - newDepth;

    max.x = max.x + newWidth;
    max.y = max.y + newHeight;
    max.z = max.z + newDepth;
}

/**
 * @brief Get child bounds for octree subdivision
 * 
 * Computes one of 8 child bounds created by subdividing the current bounds
 * at the center point. Used in octree and spatial partitioning algorithms.
 * 
 * @param index Child index (0-7) indicating which octant
 * @param center Center point for subdivision
 * @return Child bounds for the specified octant
 */
Bounds Bounds::getChildBounds(int index, const SpatioTemporalData& center) const {
    Bounds childBounds;
    
    // Determine child bounds based on bit flags in index
    if (index & 1) { // X direction bit
        childBounds.min.x = center.x;
        childBounds.max.x = max.x;
    } else {
        childBounds.min.x = min.x;
        childBounds.max.x = center.x;
    }
    
    if (index & 2) { // Y direction bit
        childBounds.min.y = center.y;
        childBounds.max.y = max.y;
    } else {
        childBounds.min.y = min.y;
        childBounds.max.y = center.y;
    }
    
    if (index & 4) { // Z direction bit
        childBounds.min.z = center.z;
        childBounds.max.z = max.z;
    } else {
        childBounds.min.z = min.z;
        childBounds.max.z = center.z;
    }
    
    // Preserve temporal dimension
    childBounds.min.time = min.time;
    childBounds.max.time = max.time;
    
    return childBounds;
}

/**
 * @brief Update bounds to include new point
 * 
 * Expands the bounds as necessary to encompass the given point.
 * If first_flag is true, initializes bounds to the point's coordinates.
 * 
 * @param point Point to include in bounds
 */
void Bounds::update(const SpatioTemporalData& point) {
    if(first_flag){
        first_flag = false;
        // Initialize bounds to point coordinates
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
    // Expand bounds to include point
    min.x = std::min(min.x, point.x);
    min.y = std::min(min.y, point.y);
    min.z = std::min(min.z, point.z);
    min.time = std::min(min.time, point.time);

    max.x = std::max(max.x, point.x);
    max.y = std::max(max.y, point.y);
    max.z = std::max(max.z, point.z);
    max.time = std::max(max.time, point.time);
}

/*
 * @brief Update bounds from JSON data
 * 
 * Reconstructs bounds from JSON object containing min/max coordinates
 * and time values. Used for deserialization from stored data.
 * 
 * @param json_data Pointer to nlohmann::json object with bounds data
 * 
 * NOTE: This function is commented out because it's not used anywhere in the codebase.
 * If JSON deserialization is needed in the future, this function can be uncommented.
 */
/*
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
*/

/**
 * @brief Enhanced containment test with boundary condition handling
 * 
 * Tests if coordinates are within bounds with special handling for
 * boundary conditions relative to maximum bounds. Uses epsilon comparison
 * for floating-point precision issues at boundaries.
 * 
 * @param x X coordinate to test
 * @param y Y coordinate to test
 * @param z Z coordinate to test
 * @param maximum Maximum bounds for boundary condition reference
 * @return True if coordinates are contained within bounds
 */
bool Bounds::contains2(const float x, const float y, const float z, const Bounds& maximum) const {
    bool xjudge, yjudge, zjudge;
    // Handle boundary conditions with epsilon tolerance
    if(abs(x - maximum.max.x) < 1e-6) xjudge = (x >= min.x && x <= max.x);
    else xjudge = (x >= min.x && x < max.x);
    if(abs(y - maximum.max.y) < 1e-6) yjudge = (y >= min.y && y <= max.y);
    else yjudge = (y >= min.y && y < max.y);
    if(abs(z - maximum.max.z) < 1e-6) zjudge = (z >= min.z && z <= max.z);
    else zjudge = (z >= min.z && z < max.z);
    return xjudge && yjudge && zjudge;
}

/**
 * @brief Print bounds information for debugging
 * 
 * Outputs the minimum and maximum coordinates in a readable format
 * for debugging and verification purposes.
 */
void Bounds::print() const {
    std::cout << min.x << " " << max.x << " / " << min.y << " " << max.y << " / " << min.z << " " << max.z << std::endl;
}

// Stream operators for Bounds serialization
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
// SpatialBounds Operations
// ============================================================================

/**
 * @brief Create maximum possible spatial bounds
 * 
 * Generates spatial bounds covering the full range of floating-point values.
 * Used as initial bounds for spatial algorithms.
 * 
 * @return Maximum spatial bounds
 */
SpatialBounds SpatialBounds::limit_max() {
    SpatialBounds result;
    result.min.x = result.min.y = result.min.z = -std::numeric_limits<float>::max();
    result.max.x = result.max.y = result.max.z = std::numeric_limits<float>::max();
    return result;
}

/**
 * @brief Create spatial bounds around center with specified dimensions
 * 
 * Constructs axis-aligned bounding box centered at the given point
 * with specified extents in each dimension.
 * 
 * @param center Center point of bounds
 * @param lengthX Extent in X dimension
 * @param lengthY Extent in Y dimension
 * @param lengthZ Extent in Z dimension
 * @return Constructed SpatialBounds
 */
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

/**
 * @brief Generate bounds with 90% overlap
 * 
 * Creates new bounds that overlap 90% with the current bounds.
 * Used for testing spatial algorithms with known overlap relationships.
 * 
 * @return New bounds with 90% overlap
 */
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

/**
 * @brief Get all 8 vertices of the bounding box
 * 
 * Generates the complete set of corner points for the 3D bounding box.
 * Useful for visualization, collision detection, and geometric operations.
 * 
 * @return Vector of 8 corner points
 */
std::vector<SpatialPoint> SpatialBounds::getVertices() const {
    std::vector<SpatialPoint> vertices;

    // Generate all 8 vertex combinations using bit manipulation
    for (int i = 0; i < 8; ++i) {
        float x = (i & 1) ? max.x : min.x;
        float y = (i & 2) ? max.y : min.y;
        float z = (i & 4) ? max.z : min.z;
        vertices.push_back(SpatialPoint(x, y, z));
    }

    return vertices;
}

// Stream operators for SpatialBounds serialization
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

// ============================================================================
// Spatial Indexing Functions
// ============================================================================

/**
 * @brief Calculate Morton (Z-order) index for 3D point
 * 
 * Computes the Morton index by recursively subdividing the bounding region
 * and determining which octant contains the point at each level. The resulting
 * index preserves spatial locality for efficient range queries.
 * 
 * @param x X coordinate of point
 * @param y Y coordinate of point
 * @param z Z coordinate of point
 * @param bound Bounding region for normalization
 * @param level Subdivision depth/precision level
 * @return 64-bit Morton index
 */
uint64_t indexOfPoint(float x, float y, float z, SpatialBounds bound, int level)
{
    uint64_t x_ = 0, y_ = 0, z_ = 0;
    auto center = bound.getCenter();
    
    // Recursive spatial subdivision
    for (auto i=level;i-- > 0;)
    {
        x_ <<= 1;
        y_ <<= 1;
        z_ <<= 1;
        
        // Determine octant and update bounds
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
    
    // Interleave bits to create final Morton index
    uint64_t index = interleaveBits(x_, y_, z_, level);
    return index;
}

// ============================================================================
// Polygon and Triangle Operations
// ============================================================================

/**
 * @brief Test if point is contained within polygon
 * 
 * Checks each triangle in the polygon to determine if the point
 * lies within the 3D extruded volume of any triangle.
 * 
 * @param point Point to test for containment
 * @return True if point is inside polygon
 */
bool Polygon::contains(const SpatialPoint& point) const {
    for (const Triangle& triangle : triangles) {
        if (isPointInTriangle(triangle, point, height)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Test if polygon intersects with bounding box
 * 
 * Checks each triangle for intersection with the given bounds.
 * Returns true if any triangle intersects the bounding box.
 * 
 * @param bound Bounding box to test for intersection
 * @return True if polygon intersects bounds
 */
bool Polygon::intersects(const SpatialBounds& bound) const {
    for (const Triangle& triangle : triangles) {
        if (isTriangleIntesectBound(triangle, bound, height)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Compute bounding box of entire polygon
 * 
 * Calculates the minimal axis-aligned bounding box that contains
 * all triangles including their extruded height.
 * 
 * @return Minimal bounding box containing polygon
 */
SpatialBounds Polygon::getBounds() const {
    if (triangles.empty()) {
        return SpatialBounds();
    }

    SpatialBounds bounds;
    bool first = true;
    
    for (const Triangle& triangle : triangles) {
        // Calculate extruded vertices
        SpatialPoint normal = triangle.normal();
        SpatialPoint p1_ext = triangle.p1 + normal * height;
        SpatialPoint p2_ext = triangle.p2 + normal * height;
        SpatialPoint p3_ext = triangle.p3 + normal * height;

        if (first) {
            bounds.min = bounds.max = triangle.p1;
            first = false;
        }
        
        // Include all vertices (original and extruded)
        bounds.updateBounds(triangle.p1);
        bounds.updateBounds(triangle.p2);
        bounds.updateBounds(triangle.p3);
        bounds.updateBounds(p1_ext);
        bounds.updateBounds(p2_ext);
        bounds.updateBounds(p3_ext);
    }

    return bounds;
}

/**
 * @brief Generate random triangle within bounds
 * 
 * Creates a triangle with three random vertices uniformly distributed
 * within the specified spatial bounds. Used for mesh generation.
 * 
 * @param bounds Spatial region for vertex generation
 * @return Random triangle within bounds
 */
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

/**
 * @brief Generate connected triangle sharing edge with previous
 * 
 * Creates a new triangle that shares two vertices with the previous triangle,
 * forming a connected mesh. The third vertex is positioned to maintain
 * surface continuity.
 * 
 * @param bounds Spatial region for vertex generation
 * @param previous Previous triangle to connect with
 * @return New triangle connected to previous
 */
Triangle Polygon::generateNextTriangle(const SpatialBounds& bounds, const Triangle& previous) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    std::uniform_real_distribution<float> dist_x(bounds.min.x, bounds.max.x);
    std::uniform_real_distribution<float> dist_y(bounds.min.y, bounds.max.y);
    std::uniform_real_distribution<float> dist_z(bounds.min.z, bounds.max.z);
    
    // Generate third point projected onto triangle plane for connectivity
    SpatialPoint normal = previous.normal();
    SpatialPoint randomPoint(dist_x(gen), dist_y(gen), dist_z(gen));
    SpatialPoint p3 = randomPoint + normal * (previous.p1 - randomPoint).dot(normal);
    
    return Triangle(previous.p1, previous.p2, p3);
}

/**
 * @brief Generate connected mesh with specified triangle count
 * 
 * Creates a polygon mesh by generating a sequence of connected triangles.
 * Each triangle shares an edge with the previous one to form a coherent surface.
 * 
 * @param bounds Spatial region for mesh generation
 * @param triangleCount Number of triangles to generate
 * @param height Extrusion height for volume operations
 * @return Generated polygon mesh
 */
Polygon Polygon::generateMesh(const SpatialBounds& bounds, int triangleCount, float height) {
    Polygon mesh;
    mesh.height = height;

    if (triangleCount > 0) {
        // Generate first triangle randomly
        Triangle first = generateRandomTriangle(bounds);
        mesh.triangles.push_back(first);

        // Generate remaining triangles connected to previous ones
        for (int i = 1; i < triangleCount; ++i) {
            Triangle next = generateNextTriangle(bounds, mesh.triangles.back());
            mesh.triangles.push_back(next);
        }
    }

    return mesh;
}

/**
 * @brief Check if a spatial point is contained within a triangle
 * 
 * Determines whether a given spatial point lies within the specified triangle
 * considering both planar containment and height constraints. The algorithm
 * first checks if the point is within the valid height range above the triangle's
 * plane, then projects the point onto the triangle's plane and tests for
 * containment using cross product calculations.
 * 
 * @param triangle The triangle to test against
 * @param point The spatial point to check
 * @param height Maximum allowed height above the triangle's plane
 * @return true if point is within triangle and height constraints, false otherwise
 */
bool isPointInTriangle(const Triangle& triangle, const SpatialPoint& point, float height) {
    // 获取三角形的法向量
    SpatialPoint normal = triangle.normal();

    // 检查点是否在三角形的平面内，并且在规定的高度范围内
    float d = (point - triangle.p1).dot(normal);
    if (d < 0 || d > height) {
        return false; // 点不在三角形所在的空间内
    }

    // 投影点到三角形平面上的位置
    // SpatialPoint projection = triangle.p1 + normal * d;
    SpatialPoint projection = point - normal * d;

    // 检查投影点是否在三角形的轮廓内
    // 判断点是否在三角形的三条边的同一侧。
    SpatialPoint edge1 = triangle.p2 - triangle.p1;
    SpatialPoint edge2 = triangle.p3 - triangle.p2;
    SpatialPoint edge3 = triangle.p1 - triangle.p3;

    SpatialPoint edge1Normal = edge1.cross(point - triangle.p1).normalize();
    SpatialPoint edge2Normal = edge2.cross(point - triangle.p2).normalize();
    SpatialPoint edge3Normal = edge3.cross(point - triangle.p3).normalize();

    float direc1 = edge1Normal.dot(normal);
    float direc2 = edge2Normal.dot(normal);
    float direc3 = edge3Normal.dot(normal);

    if (direc1 >= 0 && direc2 >= 0 && direc3 >= 0) {
        return true; // 投影点在三角形内部
    }
    if (direc1 <= 0 && direc2 <= 0 && direc3 <= 0) {
        return true; // 投影点在三角形内部
    }
    return false;
}



/**
 * @brief Check if a triangle intersects with spatial bounds
 * 
 * Determines whether the given triangle intersects or overlaps with the
 * specified spatial bounds. The function tests if any vertices of the
 * bounding box lie within the triangle considering height constraints.
 * This is useful for spatial culling and collision detection algorithms.
 * 
 * @param triangle The triangle to test intersection with
 * @param bound The spatial bounds to check against
 * @param height Maximum allowed height above the triangle's plane
 * @return true if triangle intersects with bounds, false otherwise
 */
bool isTriangleIntesectBound(const Triangle& triangle, const SpatialBounds& bound, float height){
    std::vector<SpatialPoint> points = bound.getVertices();
    for(auto &point:points){
        if(isPointInTriangle(triangle,point,height)){
            return true;
        }
    }
    return false;
}


/**
 * @brief Check if a triangle completely contains spatial bounds
 * 
 * Determines whether the given triangle completely contains all vertices
 * of the specified spatial bounds within its boundaries. All vertices of
 * the bounding box must lie within the triangle considering height constraints
 * for this function to return true. This is useful for spatial containment
 * queries and hierarchical spatial data structures.
 * 
 * @param triangle The triangle to test containment with
 * @param bound The spatial bounds to check if contained
 * @param height Maximum allowed height above the triangle's plane
 * @return true if triangle completely contains bounds, false otherwise
 */
bool isTriangleContainBound(const Triangle& triangle, const SpatialBounds& bound, float height){
    std::vector<SpatialPoint> points = bound.getVertices();
    for(auto &point:points){
        if(!isPointInTriangle(triangle,point,height)){
            return false;
        }
    }
    return true;
}


// ============================================================================
// Trajectory Operations
// ============================================================================

/**
 * @brief Serialize trajectory to JSON format
 * 
 * Converts trajectory object to JSON representation with base64-encoded
 * point data for efficient storage and transmission.
 * 
 * @return JSON object containing trajectory data
 */
nlohmann::json Trajectory::to_json() const {
    nlohmann::json json_obj;
    json_obj["id"] = id;
    
    // Serialize points as base64-encoded binary data
    std::vector<std::string> serialized_points;
    for (const auto& point : points) {
        serialized_points.push_back(base64_encode(point.to_binary()));
    }
    json_obj["points"] = serialized_points;
    return json_obj;
}

/**
 * @brief Deserialize trajectory from JSON format
 * 
 * Reconstructs trajectory object from JSON representation by decoding
 * base64-encoded point data.
 * 
 * @param json_obj JSON object containing trajectory data
 * @return Reconstructed trajectory object
 */
Trajectory Trajectory::from_json(const nlohmann::json& json_obj) {
    Trajectory traj;
    traj.id = json_obj.at("id").get<int>();
    
    // Deserialize points from base64-encoded binary data
    for (const auto& point_str : json_obj.at("points")) {
        traj.points.push_back(SpatioTemporalData::from_binary(base64_decode(point_str.get<std::string>())));
    }
    return traj;
}

/**
 * @brief Sort trajectory points by timestamp
 * 
 * Arranges points in chronological order based on their time field.
 * Essential for temporal analysis and distance calculations.
 */
void Trajectory::sort_by_timestamp() {
    std::sort(points.begin(), points.end(), [](const SpatioTemporalData& a, const SpatioTemporalData& b) {
        return a.time < b.time;
    });
}

/**
 * @brief Filter trajectory points by time range
 * 
 * Removes points outside the specified temporal window.
 * Used for temporal queries and window-based analysis.
 * 
 * @param min_time Minimum time bound (inclusive)
 * @param max_time Maximum time bound (inclusive)
 */
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

/**
 * @brief Calculate minimum distance from point to spatial bounds
 * 
 * Computes the shortest Euclidean distance from a point to the boundary
 * of a spatial bounding box. Returns 0 if point is inside bounds.
 * 
 * @param point Query point
 * @param bounds Target bounding box
 * @return Minimum distance to bounds boundary
 */
float pointToBoundsDistance(const SpatialPoint& point, const SpatialBounds& bounds) {
    float dx = std::max({bounds.min.x - point.x, 0.0f, point.x - bounds.max.x});
    float dy = std::max({bounds.min.y - point.y, 0.0f, point.y - bounds.max.y});
    float dz = std::max({bounds.min.z - point.z, 0.0f, point.z - bounds.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/**
 * @brief Calculate maximum distance from point to spatial bounds
 * 
 * Computes the longest Euclidean distance from a point to any corner
 * of the spatial bounding box. Used for conservative distance estimates.
 * 
 * @param point Query point
 * @param bounds Target bounding box
 * @return Maximum distance to bounds corners
 */
float pointToBoundsMaxDistance(const SpatialPoint& point, const SpatialBounds& bounds) {
    float dx = std::max(std::abs(point.x - bounds.min.x), std::abs(point.x - bounds.max.x));
    float dy = std::max(std::abs(point.y - bounds.min.y), std::abs(point.y - bounds.max.y));
    float dz = std::max(std::abs(point.z - bounds.min.z), std::abs(point.z - bounds.max.z));
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/**
 * @brief Calculate minimum distance from spatiotemporal point to spatial bounds
 * 
 * Overloaded version for spatiotemporal data, using only spatial coordinates
 * for distance calculation.
 * 
 * @param point Query spatiotemporal point
 * @param bounds Target spatial bounding box
 * @return Minimum distance to bounds boundary
 */
float pointToBoundsDistance(const SpatioTemporalData& point, const SpatialBounds& bounds) {
    float dx = std::max({bounds.min.x - point.x, 0.0f, point.x - bounds.max.x});
    float dy = std::max({bounds.min.y - point.y, 0.0f, point.y - bounds.max.y});
    float dz = std::max({bounds.min.z - point.z, 0.0f, point.z - bounds.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/**
 * @brief Calculate minimum distance from spatial point to spatiotemporal bounds
 * 
 * Computes distance using only spatial dimensions, ignoring temporal component.
 * 
 * @param point Query spatial point
 * @param bounds Target spatiotemporal bounding box
 * @return Minimum distance to spatial projection of bounds
 */
float pointToBoundsDistance(const SpatialPoint& point, const Bounds& bounds) {
    float dx = std::max({bounds.min.x - point.x, 0.0f, point.x - bounds.max.x});
    float dy = std::max({bounds.min.y - point.y, 0.0f, point.y - bounds.max.y});
    float dz = std::max({bounds.min.z - point.z, 0.0f, point.z - bounds.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/**
 * @brief Calculate minimum distance between spatiotemporal point and bounds
 * 
 * Computes spatial distance only, suitable for spatial queries on
 * spatiotemporal data structures.
 * 
 * @param point Query spatiotemporal point
 * @param bounds Target spatiotemporal bounding box
 * @return Minimum spatial distance to bounds boundary
 */
float pointToBoundsDistance(const SpatioTemporalData& point, const Bounds& bounds) {
    float dx = std::max({bounds.min.x - point.x, 0.0f, point.x - bounds.max.x});
    float dy = std::max({bounds.min.y - point.y, 0.0f, point.y - bounds.max.y});
    float dz = std::max({bounds.min.z - point.z, 0.0f, point.z - bounds.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ============================================================================
// Trajectory Distance Algorithms
// ============================================================================

/**
 * @brief Calculate Dynamic Time Warping (DTW) distance between point sequences
 * 
 * Implements the classic DTW algorithm for comparing temporal sequences with
 * different sampling rates or temporal alignment. Uses dynamic programming
 * to find optimal alignment path between trajectories.
 * 
 * Time complexity: O(m*n) where m,n are sequence lengths
 * Space complexity: O(m*n) for full DTW matrix
 * 
 * @param traj1 First trajectory point sequence
 * @param traj2 Second trajectory point sequence
 * @return DTW distance value (minimum cost of optimal alignment)
 */
float calculateDTWDistance(const std::vector<SpatioTemporalData>& traj1, const std::vector<SpatioTemporalData>& traj2) {
    int m = traj1.size();
    int n = traj2.size();
    
    if (m == 0 || n == 0) return std::numeric_limits<float>::infinity();
    
    // Initialize DTW matrix with infinity except [0,0]
    std::vector<std::vector<float>> dtw(m + 1, std::vector<float>(n + 1, std::numeric_limits<float>::infinity()));
    dtw[0][0] = 0;
    
    // Fill DTW matrix using dynamic programming
    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            float cost = pointToPointDistance(traj1[i - 1], traj2[j - 1]);
            dtw[i][j] = cost + std::min({dtw[i - 1][j], dtw[i][j - 1], dtw[i - 1][j - 1]});
        }
    }
    
    return dtw[m][n];
}

/**
 * @brief Calculate DTW distance between trajectory objects
 * 
 * Convenience wrapper for DTW calculation on Trajectory objects.
 * 
 * @param traj1 First trajectory
 * @param traj2 Second trajectory
 * @return DTW distance value
 */
float calculateDTWDistance(const Trajectory& traj1, const Trajectory& traj2) {
    return calculateDTWDistance(traj1.points, traj2.points);
}

/**
 * @brief Calculate cumulative nearest neighbor distance
 * 
 * For each point in the first trajectory, finds the nearest point in the
 * second trajectory and computes the average of these minimum distances.
 * Provides asymmetric similarity measure.
 * 
 * Time complexity: O(m*n) where m,n are trajectory lengths
 * 
 * @param traj1 First trajectory (query)
 * @param traj2 Second trajectory (reference)
 * @return Average nearest neighbor distance
 */
float calculateCumulativeNearestNeighborDistance(const Trajectory& traj1, const Trajectory& traj2) {
    int m = traj1.points.size();
    int n = traj2.points.size();
    
    if (m == 0 || n == 0) return std::numeric_limits<float>::infinity();

    float totalDistance = 0.0;

    // For each point in traj1, find nearest point in traj2
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

/**
 * @brief Calculate average distance between all trajectory point pairs
 * 
 * Computes the mean Euclidean distance between all possible pairs of points
 * from the two trajectories. Simple but computationally expensive measure.
 * 
 * Time complexity: O(m*n) where m,n are trajectory lengths
 * 
 * @param traj1 First trajectory
 * @param traj2 Second trajectory
 * @return Average pairwise distance
 */
float calculateAverageDistance(const Trajectory& traj1, const Trajectory& traj2) {
    if (traj1.points.empty() || traj2.points.empty()) {
        return std::numeric_limits<float>::infinity();
    }
    
    float totalDistance = 0.0;
    int count = 0;
    
    // Calculate distance between all point pairs
    for (const auto& p1 : traj1.points) {
        for (const auto& p2 : traj2.points) {
            totalDistance += pointToPointDistance(p1, p2);
            ++count;
        }
    }
    
    return totalDistance / count;
}

// ============================================================================
// Query Functions - K-Nearest Neighbor Search
// ============================================================================

/**
 * @brief Brute-force k-nearest neighbor search for trajectories
 * 
 * Performs exhaustive search through all trajectories to find k most similar
 * ones to the query trajectory. Uses parallel processing for efficiency and
 * DTW distance for robust trajectory comparison.
 * 
 * Algorithm steps:
 * 1. Filter all trajectories by time range
 * 2. Calculate DTW distance to query in parallel
 * 3. Sort by distance and return top-k results
 * 
 * @param query_center Query trajectory to match against
 * @param k Number of nearest neighbors to return
 * @param min_time Minimum time bound for trajectory filtering
 * @param max_time Maximum time bound for trajectory filtering
 * @param data Collection of trajectories to search through
 * @return Vector of k nearest trajectories with their distances
 */
std::vector<std::pair<float, Trajectory>> trajectory_kNN_query_bruteforce(
    Trajectory& query_center, int k, float min_time, float max_time, 
    std::unordered_map<int, Trajectory>& data) {
    
    // Convert map to vector for parallel processing
    std::vector<std::pair<float, Trajectory>> dataVector;
    dataVector.reserve(data.size());
    
    for (auto& [id, trajectory] : data) {
        dataVector.push_back({0, trajectory});
    }

    // Parallel distance calculation using hardware concurrency
    const int thread_pool_size = std::thread::hardware_concurrency();
    std::vector<std::future<void>> futures;
    size_t chunk_size = dataVector.size() / thread_pool_size;
    
    // Launch parallel workers for distance calculation
    for (int i = 0; i < thread_pool_size; ++i) {
        futures.push_back(std::async(std::launch::async, [&, i] {
            size_t start = i * chunk_size;
            size_t end = (i == thread_pool_size - 1) ? dataVector.size() : (i + 1) * chunk_size;
            
            // Process assigned chunk
            for (size_t j = start; j < end; ++j) {
                // Apply temporal filtering
                dataVector[j].second.cut_by_time(min_time, max_time);
                // Calculate DTW distance
                dataVector[j].first = calculateDTWDistance(query_center, dataVector[j].second);
            }
        }));
    }

    // Wait for all workers to complete
    for (auto& fut : futures) {
        fut.get();
    }

    // Sort by distance and return top-k
    std::sort(dataVector.begin(), dataVector.end(),
        [](const std::pair<float, Trajectory>& a, const std::pair<float, Trajectory>& b) {
            return a.first < b.first;
        });
    
    if (dataVector.size() > k) {
        dataVector.resize(k);
    }

    return dataVector;
}