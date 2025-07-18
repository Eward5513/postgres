/**
 * @file trace.h
 * @brief Header file for trace extension implementation functions
 * 
 * This file contains the declarations for all implementation functions that are
 * called by the PostgreSQL extension interface. These functions provide the core
 * functionality for the trace extension including data loading, index building,
 * querying, and configuration management.
 * 
 * The functions declared here bridge the gap between the PostgreSQL C interface
 * and the C++ implementation, handling type conversions and memory management
 * appropriately for both environments.
 * 
 * @author Zhang Teng
 * @date 2025
 */

#ifndef TRACE_IMPLEMENT_H
#define TRACE_IMPLEMENT_H

// ============================================================================
// PostgreSQL Headers
// ============================================================================
#include "safe_header.h"

// ============================================================================
// System and Standard Library Headers
// ============================================================================
#include <string>
#include <vector>
#include <utility>

// ============================================================================
// Data Type Constants and Enumerations
// ============================================================================

/**
 * @brief Data type bit masks for spatiotemporal data filtering
 * 
 * These constants define bit masks used to filter different types of
 * spatiotemporal data during queries. Multiple types can be combined
 * using bitwise OR operations.
 */
#define TRACE_TYPE_POINTCLOUD    0x01    ///< Point cloud data (3D points with attributes)
#define TRACE_TYPE_TRAJECTORY    0x02    ///< Trajectory data (ordered sequences of points)
#define TRACE_TYPE_MESH         0x04    ///< Mesh data (3D surfaces and volumes)

/**
 * @brief Data format enumeration for file loading
 * 
 * Defines the supported file formats for spatiotemporal data loading.
 * Used by the data loader to determine the appropriate parsing strategy.
 */
enum class DataFormat {
    AUTO_DETECT = 0,    ///< Automatically detect format from file extension
    CSV         = 1,    ///< Comma-separated values format
    JSON        = 2,    ///< JavaScript Object Notation format
    BINARY      = 3,    ///< Custom binary format for high performance
    PLY         = 4,    ///< Stanford PLY mesh format
};

/**
 * @brief Query execution status codes
 * 
 * Return codes indicating the success or failure status of query operations.
 */
enum class QueryStatus {
    SUCCESS              = 0,    ///< Query completed successfully
    NO_DATA_LOADED       = 1,    ///< No data has been loaded into the extension
    INDEX_NOT_BUILT      = 2,    ///< Spatial index has not been constructed
    INVALID_BOUNDS       = 3,    ///< Query bounds are invalid or malformed
    INSUFFICIENT_MEMORY  = 4,    ///< Not enough memory to complete query
    TIMEOUT              = 5,    ///< Query execution exceeded time limit
    INTERNAL_ERROR       = 6     ///< Internal processing error occurred
};

/**
 * @brief Spatial index type enumeration
 * 
 * Defines the types of spatial indices that can be built for query optimization.
 */
enum class IndexType {
    OCTREE     = 0,    ///< Octree spatial index (good for 3D point data)
    KDTREE     = 1,    ///< k-d tree index (efficient for k-NN queries)
    RTREE      = 2,    ///< R-tree index (optimal for range queries)
    GRID       = 3,    ///< Regular grid index (fast but memory-intensive)
    HYBRID     = 4     ///< Combination of multiple index types
};

// Simplified data structures (independent of TRACE)
struct SimpleBounds {
    float min_x, min_y, min_z, min_time;
    float max_x, max_y, max_z, max_time;
    
    SimpleBounds() : min_x(0), min_y(0), min_z(0), min_time(0),
                     max_x(0), max_y(0), max_z(0), max_time(0) {}
    
    SimpleBounds(float minx, float miny, float minz, float mint,
                 float maxx, float maxy, float maxz, float maxt)
        : min_x(minx), min_y(miny), min_z(minz), min_time(mint),
          max_x(maxx), max_y(maxy), max_z(maxz), max_time(maxt) {}
};

struct SimplePoint {
    float x, y, z, time;
    int data_type;
    int fid, pid, foreign_key;
    float intensity, speed;
    int color_r, color_g, color_b;
    
    SimplePoint() : x(0), y(0), z(0), time(0), data_type(0),
                    fid(0), pid(0), foreign_key(0),
                    intensity(0), speed(0),
                    color_r(0), color_g(0), color_b(0) {}
};

struct LoadResult {
    int files_loaded;
    long long total_points;
    float load_time_seconds;
    
    LoadResult() : files_loaded(0), total_points(0), load_time_seconds(0.0f) {}
};

struct IndexResult {
    float index_build_time;
    int chunk_count;
    int total_octree_nodes;
    int total_kdtree_nodes;
    
    IndexResult() : index_build_time(0.0f), chunk_count(0),
                    total_octree_nodes(0), total_kdtree_nodes(0) {}
};

struct StatsResult {
    int total_files;
    long long total_points;
    int total_chunks;
    float index_size_mb;
    
    StatsResult() : total_files(0), total_points(0),
                    total_chunks(0), index_size_mb(0.0f) {}
};

/**
 * @brief k-NN query result structure
 * 
 * Contains a spatial point and its distance from the query point.
 * Used for returning results from k-nearest neighbor queries.
 */
struct KnnResult {
    SimplePoint point;    ///< The found point
    float distance;       ///< Distance from query point
};

// ============================================================================
// Extension Initialization and Cleanup Functions
// ============================================================================

/**
 * @brief Initialize extension configuration
 * 
 * Sets up default configuration parameters and initializes the configuration
 * system. Called during extension startup to establish initial state.
 * 
 * @note Must be called before any other extension functions
 */
void initialize_config();

// ============================================================================
// Data Management Functions
// ============================================================================

/**
 * @brief Load spatiotemporal data from directory
 * 
 * Scans the specified directory for data files and loads them into the
 * trace extension. Supports various file formats and applies sampling
 * if specified.
 * 
 * @param directory Path to directory containing data files
 * @param max_file_num Maximum number of files to load (0 = no limit)
 * @param sample_ratio Sampling ratio (0.0-1.0, 1.0 = load all data)
 * @return LoadResult structure containing load statistics
 * 
 * @throws std::exception if directory access fails or data format is invalid
 * 
 * @example
 * LoadResult result = trace_load_data_impl("/data/traces", 100, 0.8f);
 * std::cout << "Loaded " << result.files_loaded << " files\n";
 */
LoadResult trace_load_data_impl(const std::string& directory, int max_file_num, float sample_ratio);

/**
 * @brief Clear all loaded data and reset extension state
 * 
 * Removes all loaded data from memory, clears indices, and resets the
 * extension to its initial state. Useful for cleanup or loading new datasets.
 * 
 * @return True if data was successfully cleared, false otherwise
 * 
 * @note This operation cannot be undone - all loaded data will be lost
 */
bool trace_clear_data_impl();

/**
 * @brief Get extension statistics and status information
 * 
 * Collects and returns comprehensive statistics about the current state
 * of the extension including data counts, memory usage, and index statistics.
 * 
 * @return StatsResult structure containing detailed statistics
 */
StatsResult trace_get_stats_impl();

// ============================================================================
// Index Management Functions
// ============================================================================

/**
 * @brief Build spatial and temporal indices for loaded data
 * 
 * Constructs optimized index structures (octrees, kd-trees, etc.) to enable
 * efficient spatial and temporal queries. Must be called after data loading
 * and before performing queries for optimal performance.
 * 
 * @param chunk_max_level Maximum subdivision level for spatial chunks (1-10)
 * @param octree_max_level Maximum depth for octree structures (1-20)
 * @param max_point_per_leaf Maximum points allowed in leaf nodes (1-10000)
 * @return IndexResult structure containing index build statistics
 * 
 * @throws std::exception if index construction fails or parameters are invalid
 * 
 * @note Index building is computationally intensive for large datasets
 * @note Higher subdivision levels provide faster queries but use more memory
 * 
 * @example
 * IndexResult result = trace_build_index_impl(8, 15, 100);
 * std::cout << "Built index with " << result.total_octree_nodes << " nodes\n";
 */
IndexResult trace_build_index_impl(int chunk_max_level, int octree_max_level, int max_point_per_leaf);

// ============================================================================
// Query Functions
// ============================================================================

/**
 * @brief Perform spatial range query within specified bounds
 * 
 * Searches for all spatiotemporal points that fall within the specified
 * spatial and temporal bounds. Uses spatial indices for efficient retrieval
 * when available, otherwise performs brute-force search.
 * 
 * @param bounds Spatial and temporal bounds for the query
 * @param data_type_mask Bit mask for filtering by data type (0 = all types)
 * @return Vector of points within the specified bounds
 * 
 * @throws std::exception if query bounds are invalid or no data is loaded
 * 
 * @note Performance depends on query selectivity and index quality
 * @note Large result sets may consume significant memory
 * 
 * @example
 * SimpleBounds bounds{0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 100.0f, 100.0f, 1000.0f};
 * auto points = trace_range_query_impl(bounds, TRACE_TYPE_POINTCLOUD);
 */
std::vector<SimplePoint> trace_range_query_impl(const SimpleBounds& bounds, int data_type_mask);

/**
 * @brief Perform k-nearest neighbor query around a center point
 * 
 * Finds the k nearest spatiotemporal points to the specified center point
 * within the given temporal range. Uses spatial indices and distance metrics
 * for efficient nearest neighbor search.
 * 
 * @param center Center point for the query
 * @param k Number of nearest neighbors to find (1-10000)
 * @param min_time Minimum time bound for temporal filtering
 * @param max_time Maximum time bound for temporal filtering
 * @param data_type_mask Bit mask for filtering by data type (0 = all types)
 * @return Vector of (point, distance) pairs sorted by distance
 * 
 * @throws std::exception if k is invalid, time bounds are inconsistent, or no data is loaded
 * 
 * @note Results are sorted by increasing distance from center point
 * @note May return fewer than k results if insufficient data is available
 * @note Uses Euclidean distance in 3D space
 * 
 * @example
 * SimplePoint center{50.0f, 50.0f, 50.0f, 500.0f, 1, 0, 0, 0};
 * auto neighbors = trace_knn_query_impl(center, 10, 0.0f, 1000.0f, 0);
 */
std::vector<std::pair<SimplePoint, float>> trace_knn_query_impl(const SimplePoint& center, int k,
                                                                float min_time, float max_time, int data_type_mask);

// ============================================================================
// Configuration Management Functions
// ============================================================================

/**
 * @brief Set a configuration parameter
 * 
 * Updates a configuration parameter with the specified value. Configuration
 * parameters control various aspects of the extension behavior including
 * performance tuning, logging levels, and algorithm selection.
 * 
 * @param key Configuration parameter name
 * @param value New value for the parameter (as string)
 * 
 * @throws std::exception if parameter name is invalid or value format is incorrect
 * 
 * @note Changes take effect immediately for most parameters
 * @note Some parameters may require data reload or index rebuild
 * 
 * @example
 * trace_set_config_impl("log_level", "DEBUG");
 * trace_set_config_impl("cache_size_mb", "512");
 */
void trace_set_config_impl(const std::string& key, const std::string& value);

/**
 * @brief Get a configuration parameter value
 * 
 * Retrieves the current value of a configuration parameter. Returns an
 * empty string if the parameter is not found or has not been set.
 * 
 * @param key Configuration parameter name
 * @return Current value of the parameter, or empty string if not found
 * 
 * @example
 * std::string log_level = trace_get_config_impl("log_level");
 * if (log_level.empty()) {
 *     std::cout << "Log level not configured\n";
 * }
 */
std::string trace_get_config_impl(const std::string& key);

// ============================================================================
// PostgreSQL Interface Utility Functions
// ============================================================================

/**
 * @brief Namespace containing PostgreSQL interface utility functions
 * 
 * These functions handle conversion between C++ data structures and PostgreSQL
 * tuple formats, enabling seamless integration between the extension's C++
 * implementation and PostgreSQL's C interface.
 */
namespace trace {

/**
 * @brief Create PostgreSQL tuple from LoadResult structure
 * 
 * Converts a LoadResult structure into a PostgreSQL HeapTuple suitable
 * for returning from SQL functions. Handles proper type conversion and
 * memory allocation in PostgreSQL's memory context.
 * 
 * @param result LoadResult structure to convert
 * @param tupdesc Tuple descriptor defining the output tuple structure
 * @return HeapTuple containing the result data
 * 
 * @throws PostgreSQL ERROR if tuple creation fails or descriptor is invalid
 * 
 * @note Memory is allocated in PostgreSQL's current memory context
 * @note Tuple structure must match LoadResult fields exactly
 */
HeapTuple create_load_result_tuple(const LoadResult& result, TupleDesc tupdesc);

/**
 * @brief Create PostgreSQL tuple from IndexResult structure
 * 
 * Converts an IndexResult structure into a PostgreSQL HeapTuple for
 * returning index building statistics to SQL clients.
 * 
 * @param result IndexResult structure to convert
 * @param tupdesc Tuple descriptor defining the output tuple structure
 * @return HeapTuple containing the index build statistics
 * 
 * @throws PostgreSQL ERROR if tuple creation fails
 */
HeapTuple create_index_result_tuple(const IndexResult& result, TupleDesc tupdesc);

/**
 * @brief Create PostgreSQL tuple from SimplePoint structure
 * 
 * Converts a SimplePoint (spatiotemporal point) into a PostgreSQL HeapTuple
 * for returning query results. Used by range queries and other spatial operations.
 * 
 * @param point SimplePoint structure to convert
 * @param tupdesc Tuple descriptor defining the output tuple structure
 * @return HeapTuple containing the point data
 * 
 * @throws PostgreSQL ERROR if tuple creation fails
 * 
 * @note Output tuple contains x, y, z, time, data_type, fid, pid, foreign_key fields
 */
HeapTuple create_spatiotemporal_point_tuple(const SimplePoint& point, TupleDesc tupdesc);

/**
 * @brief Create PostgreSQL tuple from KnnResult structure
 * 
 * Converts a k-NN query result (point + distance) into a PostgreSQL HeapTuple
 * for returning from k-nearest neighbor queries.
 * 
 * @param result KnnResult structure containing point and distance
 * @param tupdesc Tuple descriptor defining the output tuple structure
 * @return HeapTuple containing the k-NN result data
 * 
 * @throws PostgreSQL ERROR if tuple creation fails
 * 
 * @note Output tuple contains distance field followed by all point fields
 */
HeapTuple create_knn_result_tuple(const KnnResult& result, TupleDesc tupdesc);

/**
 * @brief Create SimpleBounds from PostgreSQL function arguments
 * 
 * Constructs a SimpleBounds structure from spatial and temporal bounds
 * passed as PostgreSQL function arguments. Handles type conversion from
 * PostgreSQL's numeric types to C++ float types.
 * 
 * @param min_x Minimum X coordinate
 * @param min_y Minimum Y coordinate
 * @param min_z Minimum Z coordinate
 * @param max_x Maximum X coordinate
 * @param max_y Maximum Y coordinate
 * @param max_z Maximum Z coordinate
 * @param min_time Minimum time value
 * @param max_time Maximum time value
 * @return SimpleBounds structure with specified bounds
 * 
 * @throws std::exception if bounds are invalid (min > max)
 * 
 * @example
 * SimpleBounds bounds = bounds_from_pg_args(0.0f, 0.0f, 0.0f, 
 *                                          100.0f, 100.0f, 100.0f,
 *                                          0.0f, 1000.0f);
 */
SimpleBounds bounds_from_pg_args(float min_x, float min_y, float min_z,
                                float max_x, float max_y, float max_z,
                                float min_time, float max_time);

/**
 * @brief Create SimplePoint from PostgreSQL function arguments
 * 
 * Constructs a SimplePoint structure from spatial and temporal coordinates
 * passed as PostgreSQL function arguments. Used for creating query center
 * points and other spatial operations.
 * 
 * @param x X coordinate
 * @param y Y coordinate
 * @param z Z coordinate
 * @param time Time value
 * @return SimplePoint structure with specified coordinates
 * 
 * @example
 * SimplePoint center = point_from_pg_args(50.0f, 50.0f, 50.0f, 500.0f);
 */
SimplePoint point_from_pg_args(float x, float y, float z, float time);

} // namespace trace

#endif // TRACE_IMPLEMENT_H