/**
 * @file query.h
 * @brief Spatial-temporal query processing interface for TRACE extension
 * 
 * This file defines the main query processing classes and functions for handling
 * spatial-temporal data operations in PostgreSQL. It provides both optimized
 * index-based queries and ground truth implementations for verification.
 * 
 * @author TRACE Development Team
 * @date 2024
 */

#ifndef TRACE_QUERY_H
#define TRACE_QUERY_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <cstdint>
#include <algorithm>

// Include actual header files instead of forward declarations
#include "../spatiotemporal_data.h"
#include "../octree_node.h"
#include "../kdtree_node_manager.h"
#include "../octree_node_manager.h"
#include "../mesh_connection_manager.h"
#include "../original_data_manager.h"
#include "../utils.h"

/**
 * @class PointSetObject
 * @brief Container class for organizing spatial-temporal data by type
 * 
 * This class serves as a unified container for different types of spatial-temporal
 * data including point clouds, trajectories, and meshes. It provides methods for
 * data organization, size calculation, and mesh connectivity establishment.
 */
class PointSetObject
{
public:
    // Member variables
    
    /**
     * @brief Container for point cloud data indexed by foreign key
     * 
     * Each entry represents a point cloud with its associated spatial-temporal points.
     * The key is the foreign_key from the original data, and the value is a PointCloud
     * object containing all points belonging to that cloud.
     */
    std::unordered_map<int, PointCloud> point_cloud;
    
    /**
     * @brief Container for trajectory data indexed by foreign key
     * 
     * Each entry represents a trajectory with its time-ordered spatial points.
     * The key is the foreign_key from the original data, and the value is a Trajectory
     * object containing all points belonging to that trajectory.
     */
    std::unordered_map<int, Trajectory> trajectory;
    
    /**
     * @brief Container for mesh data indexed by foreign key
     * 
     * Each entry represents a mesh with its vertices and connectivity information.
     * The key is the foreign_key from the original data, and the value is a Mesh
     * object containing all vertices and connection data.
     */
    std::unordered_map<int, Mesh> mesh;

    // Constructors
    
    /**
     * @brief Default constructor
     * 
     * Creates an empty PointSetObject with no data.
     */
    PointSetObject();
    
    /**
     * @brief Constructor from raw data vector
     * @param raw_data Vector of SpatioTemporalData points to be organized by type
     * 
     * Constructs a PointSetObject by categorizing the input data based on their
     * DataType (PointCloudPoint, MeshPoint, TrajectoryPoint) and organizing
     * them into appropriate containers.
     */
    PointSetObject(std::vector<SpatioTemporalData> &raw_data);
    
    /**
     * @brief Constructor from hierarchical data structure
     * @param raw_data Nested map structure: chunk_id -> octree_id -> vector of points
     * 
     * Constructs a PointSetObject from a hierarchical data structure typically
     * used in spatial indexing systems. The data is flattened and organized
     * by data type and foreign key.
     */
    PointSetObject(std::unordered_map<int, std::unordered_map<int, std::vector<SpatioTemporalData>>> &raw_data);
    
    /**
     * @brief Constructor from vector of point vectors
     * @param raw_data Vector of vectors, where each inner vector contains related points
     * 
     * Constructs a PointSetObject from chunked data where each chunk is a vector
     * of related spatial-temporal points. Useful for batch processing scenarios.
     */
    PointSetObject(std::vector<std::vector<SpatioTemporalData>> &raw_data);

    // Member functions
    
    /**
     * @brief Get the total number of unique objects (point clouds + trajectories + meshes)
     * @return Total count of distinct objects across all data types
     * 
     * Returns the sum of unique objects in all three containers. This represents
     * the number of distinct spatial-temporal entities in the dataset.
     */
    std::int64_t key_size();
    
    /**
     * @brief Get the total number of individual data points
     * @return Total count of all spatial-temporal data points
     * 
     * Returns the total number of individual SpatioTemporalData points across
     * all point clouds, trajectories, and meshes. This represents the raw
     * data volume in the dataset.
     */
    std::int64_t size();
    
    /**
     * @brief Establish mesh connectivity information
     * 
     * For each mesh in the mesh container, this function loads the connectivity
     * information from the database. This includes triangle/face definitions
     * that define how mesh vertices are connected to form surfaces.
     * 
     * @note This operation involves database I/O and may take significant time
     *       for large mesh datasets. The function processes meshes serially.
     */
    void connecting_mesh();
};

/**
 * @brief Compare two PointSetObject instances for equality
 * @param first First PointSetObject to compare
 * @param second Second PointSetObject to compare
 * @return true if both objects contain the same data, false otherwise
 * 
 * Performs a deep comparison of two PointSetObject instances, checking that
 * they contain the same point clouds, trajectories, and meshes with identical
 * spatial-temporal data. Used primarily for testing and validation.
 */
bool is_equal(PointSetObject &first, PointSetObject &second);

/**
 * @class IndexFront
 * @brief Main query processing engine with spatial-temporal indexing
 * 
 * This class provides an optimized query processing interface that leverages
 * spatial-temporal indexing structures (octrees, KD-trees, metric trees) to
 * efficiently answer various types of queries on large spatial-temporal datasets.
 * 
 * The class maintains multiple index structures and provides both insertion/deletion
 * operations and various query types including range queries, k-NN queries,
 * similarity queries, and mesh-based queries.
 */
class IndexFront
{
public:
    /// Type alias for query result objects
    using result_type = PointSetObject;
    
    // Member variables
    
    /**
     * @brief Sample cells for approximate query processing
     * 
     * Array of sample cell counts used for approximate query processing.
     * Each element represents the number of sampled points in a spatial cell,
     * enabling fast approximate answers for certain query types.
     */
    std::vector<uint32_t> sampleCells;
    
    /**
     * @brief Original cells for exact query processing
     * 
     * Array of original cell counts containing the actual number of points
     * in each spatial cell. Used for exact query processing and to determine
     * when sampling ratios need adjustment.
     */
    std::vector<uint64_t> originalCells;
    
    /**
     * @brief Global spatial bounds of the entire dataset
     * 
     * Defines the minimum and maximum coordinates that bound all spatial-temporal
     * data in the system. Used for spatial partitioning and query optimization.
     */
    SpatialBounds global_bound;
    
    /**
     * @brief Block size statistics for spatial partitions
     * 
     * Nested map structure tracking the size of data blocks:
     * octree_id -> kdtree_id -> block_size
     * Used for load balancing and query optimization decisions.
     */
    std::unordered_map<int, std::unordered_map<int, std::int64_t>> block_size;
    
    /**
     * @brief Changed block size statistics for incremental updates
     * 
     * Tracks changes in block sizes since last optimization cycle.
     * Used to determine when spatial partitions need rebalancing.
     */
    std::unordered_map<int, std::unordered_map<int, std::int64_t>> changed_block_size;
    
    /**
     * @brief Binary key-value storage for cached index structures
     * 
     * High-performance storage system for serialized index structures
     * and intermediate query results. Provides fast access to frequently
     * used spatial index nodes.
     */
    BinaryKVStorage bs;
    
    /**
     * @brief Database operation timing statistics
     * 
     * Accumulates time spent on database I/O operations during query
     * processing. Used for performance monitoring and optimization.
     */
    double db_time;
    
    /**
     * @brief Query processing timing statistics
     * 
     * Measures pure query processing time excluding database I/O.
     * Used for performance analysis and bottleneck identification.
     */
    double query_time;
    
    /**
     * @brief Data formatting timing statistics
     * 
     * Tracks time spent on data format conversion and result preparation.
     * Helps identify formatting bottlenecks in query pipelines.
     */
    double format_time;

    // Constructor
    
    /**
     * @brief Constructor for IndexFront
     * 
     * Initializes the query processing engine with default configurations.
     * Sets up distance functions for trajectory and point cloud comparisons,
     * and prepares internal data structures for query processing.
     */
    IndexFront();

    // Insert operations
    
    /**
     * @brief Insert a single spatial-temporal data point
     * @param point The SpatioTemporalData point to insert
     * @return Auto-deduced return type (typically void or status indicator)
     * 
     * Inserts a single point into the spatial-temporal index structures.
     * The point is routed to appropriate spatial partitions and may trigger
     * sampling decisions based on current sampling ratios.
     */
    auto insert(const SpatioTemporalData &point);
    
    /**
     * @brief Insert multiple spatial-temporal data points
     * @param points Vector of SpatioTemporalData points to insert
     * @return Auto-deduced return type (typically void or status indicator)
     * 
     * Efficiently inserts multiple points in batch mode. Points are grouped
     * by spatial partition to minimize database I/O operations and optimize
     * index structure updates.
     */
    auto insert(const std::vector<SpatioTemporalData> &points);

    // Remove operations
    
    /**
     * @brief Remove multiple spatial-temporal data points
     * @param points Vector of SpatioTemporalData points to remove
     * @return Auto-deduced return type (typically void or status indicator)
     * 
     * Removes the specified points from the spatial-temporal index structures.
     * Uses soft deletion by marking points as deleted rather than physically
     * removing them, which preserves index structure integrity.
     */
    auto remove(const std::vector<SpatioTemporalData> &points);

    // Query operations
    
    /**
     * @brief Execute a spatial-temporal range query
     * @param bound Spatial bounds defining the query region
     * @param min_time Minimum timestamp for temporal filtering
     * @param max_time Maximum timestamp for temporal filtering
     * @param type Data type filter (PointCloudPoint, MeshPoint, TrajectoryPoint)
     * @return PointSetObject containing all points within the specified range
     * 
     * Finds all spatial-temporal points that fall within the specified spatial
     * bounds and temporal range. Uses spatial index structures to efficiently
     * prune search space and avoid full data scans.
     */
    result_type range_query(SpatialBounds &bound, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a mesh-based spatial query
     * @param polygon Polygon defining the query region
     * @param min_time Minimum timestamp for temporal filtering
     * @param max_time Maximum timestamp for temporal filtering
     * @param type Data type filter
     * @return PointSetObject containing points within the polygon region
     * 
     * Finds all points that fall within a complex polygonal region. More
     * sophisticated than rectangular range queries, this supports arbitrary
     * polygon shapes for precise spatial selection.
     */
    result_type mesh_query(Polygon &polygon, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a buffer query around a spatial region
     * @param bound Base spatial bounds
     * @param buffer_size Distance to extend bounds in all directions
     * @param min_time Minimum timestamp for temporal filtering
     * @param max_time Maximum timestamp for temporal filtering
     * @param type Data type filter
     * @return PointSetObject containing points within the buffered region
     * 
     * Extends the specified spatial bounds by buffer_size in all directions
     * and returns all points within the expanded region. Useful for proximity
     * analysis and spatial join operations.
     */
    result_type buffer_query(SpatialBounds &bound, float buffer_size, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a k-nearest neighbors query (raw results)
     * @param center Center point for distance calculations
     * @param k Number of nearest neighbors to find
     * @param min_time Minimum timestamp for temporal filtering
     * @param max_time Maximum timestamp for temporal filtering
     * @param type Data type filter
     * @return Auto-deduced return type containing raw k-NN results
     * 
     * Finds the k closest points to the specified center point within the
     * given temporal range. Returns raw results without organizing into
     * PointSetObject structure, useful for distance-based analysis.
     */
    auto kNN_query_raw(SpatialPoint &center, int k, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a k-nearest neighbors query
     * @param center Center point for distance calculations
     * @param k Number of nearest neighbors to find
     * @param min_time Minimum timestamp for temporal filtering
     * @param max_time Maximum timestamp for temporal filtering
     * @param type Data type filter
     * @return PointSetObject containing the k nearest points
     * 
     * Finds the k closest points to the specified center point and organizes
     * them into a PointSetObject for consistent result handling.
     */
    result_type kNN_query(SpatialPoint &center, int k, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a user-specific query
     * @param user_id Identifier for the specific user
     * @param min_time Minimum timestamp for temporal filtering
     * @param max_time Maximum timestamp for temporal filtering
     * @param type Data type filter
     * @return PointSetObject containing all data belonging to the specified user
     * 
     * Retrieves all spatial-temporal data associated with a specific user
     * within the given temporal range. Useful for user-centric data analysis
     * and privacy-aware querying.
     */
    result_type user_query(user_id_t user_id, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a similarity-based query
     * @param bound Spatial bounds to limit search scope
     * @param center Reference pattern for similarity comparison
     * @param k Number of most similar results to return
     * @param min_time Minimum timestamp for temporal filtering
     * @param max_time Maximum timestamp for temporal filtering
     * @param type Data type filter
     * @return PointSetObject containing the k most similar data objects
     * 
     * Finds the k most similar spatial-temporal patterns to the provided
     * reference pattern within the specified bounds and temporal range.
     * Uses advanced similarity metrics appropriate for the data type.
     */
    result_type similarity_query(SpatialBounds &bound, std::vector<SpatioTemporalData> &center, int k, float min_time,
                                 float max_time, DataType type);

private:
    /**
     * @brief Distance function for trajectory comparison
     * 
     * Function pointer to the algorithm used for computing distances between
     * trajectory objects. Typically implements DTW (Dynamic Time Warping)
     * or similar trajectory-specific distance metrics.
     */
    std::function<float(const std::vector<SpatioTemporalData> &, const std::vector<SpatioTemporalData> &)>
        trajectory_distance;
    
    /**
     * @brief Distance function for point cloud comparison
     * 
     * Function pointer to the algorithm used for computing distances between
     * point cloud objects. Typically implements Hausdorff distance or
     * similar point set distance metrics.
     */
    std::function<float(const std::vector<SpatioTemporalData> &, const std::vector<SpatioTemporalData> &)>
        point_cloud_distance;
    
    /**
     * @brief Thread pool for parallel processing
     * 
     * Manages worker threads for parallelizing computationally intensive
     * operations like distance calculations and index updates.
     * Note: Currently used in serial mode for stability.
     */
    ThreadPoolWrapper thread_pool;
    
    /**
     * @brief Octree nodes for spatial indexing
     * 
     * Vector of octree nodes that form the primary spatial index structure.
     * Provides hierarchical spatial partitioning for efficient spatial queries.
     */
    std::vector<DBOctreeNode> octree_nodes;
    
    /**
     * @brief Metric tree index for trajectory similarity queries
     * 
     * Specialized index structure optimized for trajectory similarity searches.
     * Uses metric space properties to accelerate k-NN and similarity queries
     * on trajectory data.
     */
    MetricTree trajectory_metric_index;
    
    /**
     * @brief Timer for performance measurement
     * 
     * High-resolution timer used for measuring execution times of various
     * operations. Supports performance profiling and optimization efforts.
     */
    TimerClock tc;
};

/**
 * @class GroundTruth
 * @brief Ground truth query processor for validation and testing
 * 
 * This class provides reference implementations of all query types without
 * using spatial-temporal indexing. It performs exhaustive searches to generate
 * correct results that can be used to validate the optimized IndexFront
 * implementation.
 * 
 * All query methods have identical signatures to IndexFront but use brute-force
 * algorithms to ensure correctness at the cost of performance.
 */
class GroundTruth
{
public:
    /// Type alias for query result objects
    using result_type = PointSetObject;

    // Constructor
    
    /**
     * @brief Constructor for GroundTruth
     * 
     * Initializes the ground truth query processor with basic configurations.
     * Sets up distance functions identical to IndexFront to ensure consistent
     * similarity calculations.
     */
    GroundTruth();

    // Query operations (all identical to IndexFront but with brute-force implementations)
    
    /**
     * @brief Execute a ground truth spatial-temporal range query
     * 
     * Brute-force implementation that checks every point in the dataset
     * against the specified spatial and temporal bounds. Guarantees correct
     * results but has O(n) time complexity.
     * 
     * @see IndexFront::range_query for parameter descriptions
     */
    result_type range_query(SpatialBounds &bound, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a ground truth mesh-based spatial query
     * 
     * @see IndexFront::mesh_query for parameter descriptions
     */
    result_type mesh_query(Polygon &polygon, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a ground truth buffer query
     * 
     * @see IndexFront::buffer_query for parameter descriptions
     */
    result_type buffer_query(SpatialBounds &bound, float buffer_size, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a ground truth k-nearest neighbors query (raw results)
     * 
     * @see IndexFront::kNN_query_raw for parameter descriptions
     */
    auto kNN_query_raw(SpatialPoint &center, int k, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a ground truth k-nearest neighbors query
     * 
     * @see IndexFront::kNN_query for parameter descriptions
     */
    result_type kNN_query(SpatialPoint &center, int k, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a ground truth user-specific query
     * 
     * @see IndexFront::user_query for parameter descriptions
     */
    result_type user_query(user_id_t user_id, float min_time, float max_time, DataType type);
    
    /**
     * @brief Execute a ground truth similarity-based query
     * 
     * @see IndexFront::similarity_query for parameter descriptions
     */
    result_type similarity_query(SpatialBounds &bound, std::vector<SpatioTemporalData> &center, int k, float min_time,
                                 float max_time, DataType type);

private:
    /**
     * @brief Distance function for trajectory comparison (identical to IndexFront)
     */
    std::function<float(const std::vector<SpatioTemporalData> &, const std::vector<SpatioTemporalData> &)>
        trajectory_distance;
    
    /**
     * @brief Distance function for point cloud comparison (identical to IndexFront)
     */
    std::function<float(const std::vector<SpatioTemporalData> &, const std::vector<SpatioTemporalData> &)>
        point_cloud_distance;
    
    /**
     * @brief Thread pool for parallel processing (used serially in ground truth)
     */
    ThreadPoolWrapper thread_pool;
};

#endif // TRACE_QUERY_H 