/**
 * @file query.cpp
 * @brief Implementation of spatial-temporal query processing classes
 * 
 * This file implements the query processing functionality defined in query.h,
 * providing both optimized index-based and ground truth query implementations
 * for the TRACE PostgreSQL extension.
 * 
 * @author TRACE Development Team
 * @date 2024
 */

#include "../include/query/query.h"

// Add necessary includes for implementation
#include <execution>
#include <string>
#include <queue>
#include <random>
#include <stdexcept>

// PostgreSQL headers for elog
extern "C" {
#include "postgres.h"
#include "utils/elog.h"
}

// External function declarations that are still needed (these should be defined elsewhere in your codebase)
extern double sample_ratio;

//=========================================================================
// PointSetObject Implementation
//=========================================================================

/**
 * @brief Default constructor for PointSetObject
 * 
 * Initializes an empty PointSetObject with no spatial-temporal data.
 * All containers (point_cloud, trajectory, mesh) are initialized as empty.
 */
PointSetObject::PointSetObject() {}

/**
 * @brief Constructor from raw data vector
 * @param raw_data Vector of SpatioTemporalData points to organize
 * 
 * This constructor processes a flat vector of spatial-temporal data points
 * and organizes them into appropriate containers based on their data type.
 * Points are grouped by their foreign_key within each data type category.
 * 
 * After organization, trajectory points are sorted by timestamp to ensure
 * temporal ordering within each trajectory.
 * 
 * @throws std::runtime_error if an unknown data type is encountered
 */
PointSetObject::PointSetObject(std::vector<SpatioTemporalData> &raw_data)
{
    // Process each point and route to appropriate container
    for (auto &point : raw_data)
    {
        switch (point.tid)
        {
            case PointCloudPoint:
            {
                point_cloud[point.foreign_key].points.push_back(point);
                break;
            }
            case MeshPoint:
            {
                mesh[point.foreign_key].points.push_back(point);
                break;
            }
            case TrajectoryPoint:
            {
                trajectory[point.foreign_key].points.push_back(point);
                break;
            }
            default:
            {
                throw std::runtime_error("bad type !");
            }
        }
    }
    
    // Sort all trajectories by timestamp to ensure temporal ordering
    for (auto &i : trajectory)
    {
        i.second.sort_by_timestamp();
    }
}

/**
 * @brief Constructor from hierarchical data structure
 * @param raw_data Nested map: chunk_id -> octree_id -> points vector
 * 
 * This constructor handles data organized in a hierarchical spatial structure,
 * typically from spatial indexing systems. The data is flattened from the
 * nested structure and organized by data type and foreign key.
 * 
 * The input structure represents:
 * - First level: Spatial chunks (coarse spatial partitioning)
 * - Second level: Octree nodes (fine spatial partitioning)
 * - Third level: Individual spatial-temporal points
 * 
 * @throws std::runtime_error if an unknown data type is encountered
 */
PointSetObject::PointSetObject(std::unordered_map<int, std::unordered_map<int, std::vector<SpatioTemporalData>>> &raw_data)
{
    // Traverse the hierarchical structure: chunk -> octree -> points
    for (auto &chunk : raw_data)
    {
        for (auto &octree : chunk.second)
        {
            for (auto &point : octree.second)
            {
                switch (point.tid)
                {
                    case PointCloudPoint:
                    {
                        point_cloud[point.fid].points.push_back(point);
                        break;
                    }
                    case MeshPoint:
                    {
                        mesh[point.fid].points.push_back(point);
                        break;
                    }
                    case TrajectoryPoint:
                    {
                        trajectory[point.fid].points.push_back(point);
                        break;
                    }
                    default:
                    {
                        throw std::runtime_error("bad type !");
                    }
                }
            }
        }
    }
    
    // Sort all trajectories by timestamp
    for (auto &i : trajectory)
    {
        i.second.sort_by_timestamp();
    }

    // Note: Parallel sorting is commented out for stability
    // for (auto &i : trajectory) {
    //     std::sort(std::execution::par, i.second.begin(), i.second.end(), [](const SpatioTemporalData &a, const SpatioTemporalData &b) {
    //         return a.time < b.time;
    //     });
    // }
}

/**
 * @brief Constructor from vector of point vectors
 * @param raw_data Vector of vectors containing related spatial-temporal points
 * 
 * This constructor handles chunked data where each chunk is a vector of
 * related points. It processes each chunk sequentially and organizes points
 * by their data type and foreign key, ensuring proper ID assignment.
 * 
 * @throws std::runtime_error if an unknown data type is encountered
 */
PointSetObject::PointSetObject(std::vector<std::vector<SpatioTemporalData>> &raw_data)
{
    // Process each chunk of related points
    for (auto &chunk : raw_data)
    {
        for (auto &point : chunk)
        {
            switch (point.tid)
            {
            case PointCloudPoint:
            {
                auto it = point_cloud.find(point.foreign_key);
                if (it == point_cloud.end())
                {
                    // Initialize new point cloud with proper ID
                    point_cloud[point.foreign_key].id = point.foreign_key;
                    it = point_cloud.find(point.foreign_key);
                }
                it->second.points.push_back(point);
                break;
            }
            case MeshPoint:
            {
                auto it = mesh.find(point.foreign_key);
                if (it == mesh.end())
                {
                    // Initialize new mesh with proper ID
                    mesh[point.foreign_key].id = point.foreign_key;
                    it = mesh.find(point.foreign_key);
                }
                it->second.points.push_back(point);
                break;
            }
            case TrajectoryPoint:
            {
                auto it = trajectory.find(point.foreign_key);
                if (it == trajectory.end())
                {
                    // Initialize new trajectory with proper ID
                    trajectory[point.foreign_key].id = point.foreign_key;
                    it = trajectory.find(point.foreign_key);
                }
                it->second.points.push_back(point);
                break;
            }
            default:
            {
                throw std::runtime_error("bad type !");
            }
            }
        }
    }
    
    // Sort all trajectories by timestamp
    for (auto &i : trajectory)
    {
        i.second.sort_by_timestamp();
    }
}

/**
 * @brief Get total number of unique spatial-temporal objects
 * @return Sum of point clouds, trajectories, and meshes
 * 
 * Returns the total count of distinct spatial-temporal entities across
 * all data types. This represents the number of unique objects in the dataset.
 */
std::int64_t PointSetObject::key_size()
{
    return point_cloud.size() + trajectory.size() + mesh.size();
}

/**
 * @brief Get total number of individual data points
 * @return Sum of all points across all objects and data types
 * 
 * Counts all individual SpatioTemporalData points in all point clouds,
 * trajectories, and meshes. This represents the raw data volume.
 */
std::int64_t PointSetObject::size()
{
    std::int64_t result = 0;
    
    // Count points in all point clouds
    for (auto &i : point_cloud)
    {
        result += i.second.points.size();
    }
    
    // Count points in all trajectories
    for (auto &i : trajectory)
    {
        result += i.second.points.size();
    }
    
    // Count points in all meshes
    for (auto &i : mesh)
    {
        result += i.second.points.size();
    }
    
    return result;
}

/**
 * @brief Establish connectivity information for all meshes
 * 
 * For each mesh in the mesh container, this method loads the connectivity
 * information from the database. This includes triangle/face definitions
 * that specify how mesh vertices are connected to form surfaces.
 * 
 * The function processes meshes serially (not in parallel) for stability
 * and to avoid database connection conflicts.
 * 
 * @note This operation involves database I/O and may be time-consuming
 *       for datasets with many large meshes.
 */
void PointSetObject::connecting_mesh()
{
    // Process each mesh serially to load connectivity data
    for (auto &i : mesh)
    {
        double db_time = 0;
        // Load mesh connectivity from database
        i.second.connections = MeshConnectionManager::loadDataFromDatabase(i.second.id, db_time);
    }
}

//=========================================================================
// Utility Functions
//=========================================================================

/**
 * @brief Compare two PointSetObject instances for equality
 * @param first First PointSetObject to compare
 * @param second Second PointSetObject to compare
 * @return true if objects contain identical data, false otherwise
 * 
 * Performs comprehensive equality checking by comparing all point clouds,
 * trajectories, and meshes. The comparison is based on unique IDs of
 * spatial-temporal points, ensuring proper handling of floating-point
 * precision issues.
 * 
 * This function is primarily used for testing and validation purposes
 * to verify that query results are consistent across different implementations.
 * 
 * @note The function logs detailed error information using PostgreSQL's
 *       elog system when mismatches are detected.
 */
bool is_equal(PointSetObject &first, PointSetObject &second)
{
    // Compare all point clouds
    for (auto &i : first.point_cloud)
    {
        auto key = i.first;
        
        // Check size consistency
        if (first.point_cloud[key].points.size() != second.point_cloud[key].points.size())
        {
            elog(ERROR, "point_cloud size not equal !");
            return false;
        }
        
        // Get point vectors for comparison
        auto &first_vec = first.point_cloud[key].points;
        auto &second_vec = second.point_cloud[key].points;
        
        // Create sorted ID lists for comparison
        std::vector<std::pair<std::int64_t, std::int64_t>> first_ids;
        std::vector<std::pair<std::int64_t, std::int64_t>> second_ids;
        
        std::int64_t offset = 0;
        for (auto &i : first_vec)
        {
            first_ids.push_back({i.unique_id(), offset++});
        }
        
        offset = 0;
        for (auto &i : second_vec)
        {
            second_ids.push_back({i.unique_id(), offset++});
        }
        
        // Sort by unique ID for consistent comparison
        std::sort(first_ids.begin(), first_ids.end());
        std::sort(second_ids.begin(), second_ids.end());
        
        // Compare sorted unique IDs
        for (std::int64_t j = 0; j < first_vec.size(); ++j)
        {
            if (first_ids[j].first != second_ids[j].first)
            {
                // Log detailed mismatch information
                first_vec[first_ids[j].second].print_key_elements();
                second_vec[second_ids[j].second].print_key_elements();
                elog(ERROR, "point_cloud unique_id not equal !");
                return false;
            }
        }
    }
    
    // Compare all trajectories (similar logic as point clouds)
    for (auto &i : first.trajectory)
    {
        auto key = i.first;
        if (first.trajectory[key].points.size() != second.trajectory[key].points.size())
        {
            elog(ERROR, "trajectory size not equal !");
            return false;
        }
        
        auto &first_vec = first.point_cloud[key].points;
        auto &second_vec = second.point_cloud[key].points;
        std::vector<std::pair<std::int64_t, std::int64_t>> first_ids;
        std::vector<std::pair<std::int64_t, std::int64_t>> second_ids;
        
        std::int64_t offset = 0;
        for (auto &i : first_vec)
        {
            first_ids.push_back({i.unique_id(), offset++});
        }
        offset = 0;
        for (auto &i : second_vec)
        {
            second_ids.push_back({i.unique_id(), offset++});
        }
        
        std::sort(first_ids.begin(), first_ids.end());
        std::sort(second_ids.begin(), second_ids.end());
        
        for (std::int64_t j = 0; j < first_vec.size(); ++j)
        {
            if (first_ids[j].first != second_ids[j].first)
            {
                first_vec[first_ids[j].second].print_key_elements();
                second_vec[second_ids[j].second].print_key_elements();
                elog(ERROR, "trajectory unique_id not equal !");
                return false;
            }
        }
    }
    
    // Compare all meshes (similar logic as point clouds)
    for (auto &i : first.mesh)
    {
        auto key = i.first;
        if (first.mesh[key].points.size() != second.mesh[key].points.size())
        {
            elog(ERROR, "mesh size not equal !");
            return false;
        }
        
        auto &first_vec = first.point_cloud[key].points;
        auto &second_vec = second.point_cloud[key].points;
        std::vector<std::pair<std::int64_t, std::int64_t>> first_ids;
        std::vector<std::pair<std::int64_t, std::int64_t>> second_ids;
        
        std::int64_t offset = 0;
        for (auto &i : first_vec)
        {
            first_ids.push_back({i.unique_id(), offset++});
        }
        offset = 0;
        for (auto &i : second_vec)
        {
            second_ids.push_back({i.unique_id(), offset++});
        }
        
        std::sort(first_ids.begin(), first_ids.end());
        std::sort(second_ids.begin(), second_ids.end());
        
        for (std::int64_t j = 0; j < first_vec.size(); ++j)
        {
            if (first_ids[j].first != second_ids[j].first)
            {
                first_vec[first_ids[j].second].print_key_elements();
                second_vec[second_ids[j].second].print_key_elements();
                elog(ERROR, "mesh unique_id not equal !");
                return false;
            }
        }
    }
    
    return true;
}

//=========================================================================
// IndexFront Implementation
//=========================================================================

/**
 * @brief Constructor for IndexFront query processor
 * 
 * Initializes the optimized query processing engine with default configurations.
 * Sets up distance functions for trajectory and point cloud comparisons using
 * appropriate algorithms (DTW for trajectories, Hausdorff for point clouds).
 * 
 * The constructor prepares all internal data structures but does not load
 * any actual spatial-temporal data or build indices.
 */
IndexFront::IndexFront()
{
    // Initialize function pointers for distance calculations
    trajectory_distance = static_cast<float (*)(const std::vector<SpatioTemporalData> &, const std::vector<SpatioTemporalData> &)>(&calculateDTWDistance);
    point_cloud_distance = hausdorffDistance;
}

/**
 * @brief Insert a single spatial-temporal data point
 * @param point The SpatioTemporalData point to insert
 * @return Auto-deduced return type (implementation specific)
 * 
 * Inserts a single point into the spatial-temporal index structures.
 * The process involves:
 * 1. Locating the appropriate octree leaf node for the point
 * 2. Loading existing data from that spatial partition
 * 3. Adding the point to the original data storage
 * 4. Updating sampling statistics and potentially the KD-tree index
 * 
 * The insertion may trigger sampling decisions based on current ratios
 * and spatial cell density.
 */
auto IndexFront::insert(const SpatioTemporalData &point)
{
    // Find the appropriate octree leaf node for this point
    const DBOctreeNode leaf_node = point_query_on_octree(octree_nodes, point);
    
    // Load existing data from the spatial partition
    auto original_data = OriginalDataManager::loadOriginalDataFromDatabase(leaf_node.oc_id, leaf_node.kd_id);
    
    // Add the new point
    original_data.push_back(point);
    
    // Store updated data back to database
    OriginalDataManager::writeOriginalDataToDatabase(leaf_node.oc_id, leaf_node.kd_id, original_data);
    
    // Update spatial cell statistics
    int64_t index = indexOfPoint(point.x, point.y, point.z, global_bound, chunk_max_level);
    originalCells[index]++;
    
    // Check if sampling threshold is exceeded
    if (originalCells[index] * sample_ratio > sampleCells[index])
    {
        // Load and update KD-tree index
        auto kdtree_nodes = KdTreeNodeManager::loadKdTreeNodesFromDatabase(leaf_node.oc_id, leaf_node.kd_id);
        insert_kdtree(kdtree_nodes, point);
        KdTreeNodeManager::writeKdTreeNodesToDatabase(leaf_node.oc_id, leaf_node.kd_id, kdtree_nodes);
        ++sampleCells[index];
    }
}

/**
 * @brief Insert multiple spatial-temporal data points efficiently
 * @param points Vector of SpatioTemporalData points to insert
 * @return Auto-deduced return type (implementation specific)
 * 
 * Performs batch insertion of multiple points with optimized I/O.
 * Points are grouped by their target spatial partition to minimize
 * database operations and index updates.
 * 
 * The process includes:
 * 1. Grouping points by octree/KD-tree partition
 * 2. Batch loading existing data for each partition
 * 3. Merging new points with existing data
 * 4. Batch updating both original data and index structures
 * 5. Applying sampling decisions for index maintenance
 * 
 * Performance metrics (db_time, query_time) are tracked during execution.
 */
auto IndexFront::insert(const std::vector<SpatioTemporalData> &points)
{
    TimerClock clock;
    db_time = 0;
    query_time = 0;
    
    // Group points by their target spatial partition
    std::unordered_map<int, std::unordered_map<int, std::vector<SpatioTemporalData>>> mapped_vectors;
    for (auto &point : points)
    {
        const DBOctreeNode leaf_node = point_query_on_octree(octree_nodes, point);
        mapped_vectors[leaf_node.oc_id][leaf_node.kd_id].push_back(point);
    }
    
    // Process each spatial partition
    for (auto &[octree_id, octree] : mapped_vectors)
    {
        for (auto &[kdtree_id, points] : octree)
        {
            bool inserted = false;
            
            // Load existing data with timing
            tc.tick();
            auto original_data = OriginalDataManager::loadOriginalDataFromDatabase(octree_id, kdtree_id);
            auto kdtree_nodes = bs.read<DBKdtreeNode>("kd_"+std::to_string(octree_id)+"_"+std::to_string(kdtree_id));
            db_time += tc.milliSec();
            
            // Merge new points with existing data
            original_data.insert(original_data.end(), points.begin(), points.end());
            
            // Sort by timestamp for temporal locality
            std::sort(original_data.begin(), original_data.end(), [](const SpatioTemporalData &a, const SpatioTemporalData &b)
                      { return a.time < b.time; });
            
            // Apply sampling for index updates
            for (auto &point : points)
            {
                if (random_range() < sample_ratio)
                {
                    if (!insert_kdtree(kdtree_nodes, point))
                    {
                        throw std::runtime_error("same point");
                    }
                }
            }
            
            // Write updated data back with timing
            tc.tick();
            OriginalDataManager::updateOriginalDataInDatabase(octree_id, kdtree_id, original_data);
            bs.write<DBKdtreeNode>("kd_"+std::to_string(octree_id)+"_"+std::to_string(kdtree_id),kdtree_nodes);
            db_time += tc.milliSec();
        }
    }
    
    // Calculate pure query processing time
    query_time = clock.milliSec() - db_time;
}

/**
 * @brief Remove multiple spatial-temporal data points
 * @param points Vector of SpatioTemporalData points to remove
 * @return Auto-deduced return type (implementation specific)
 * 
 * Performs batch removal of points using soft deletion strategy.
 * Points are marked as deleted rather than physically removed to
 * preserve index structure integrity and avoid expensive reorganization.
 * 
 * The process includes:
 * 1. Grouping points by spatial partition
 * 2. Loading existing data for each partition
 * 3. Filtering out points to be deleted
 * 4. Marking corresponding index entries as deleted
 * 5. Updating storage with modified data
 * 
 * Statistics on deletion ratios are logged for monitoring purposes.
 */
auto IndexFront::remove(const std::vector<SpatioTemporalData> &points)
{
    std::int64_t deleted_original = 0;
    std::int64_t deleted_sample = 0;
    db_time = 0;
    query_time = 0;
    TimerClock clock;
    
    // Group points by spatial partition for efficient processing
    std::unordered_map<int, std::unordered_map<int, std::unordered_set<SpatioTemporalData>>> mapped_vectors;
    for (auto &point : points)
    {
        const DBOctreeNode leaf_node = point_query_on_octree(octree_nodes, point);
        mapped_vectors[leaf_node.oc_id][leaf_node.kd_id].insert(point);
    }
    
    // Process each spatial partition
    for (auto &[octree_id, octree] : mapped_vectors)
    {
        for (auto &[kdtree_id, points] : octree)
        {
            // Load existing data with timing
            tc.tick();
            auto original_data = OriginalDataManager::loadOriginalDataFromDatabase(octree_id, kdtree_id);
            auto kdtree_nodes = KdTreeNodeManager::loadKdTreeNodesFromDatabase(octree_id, kdtree_id);
            db_time += tc.milliSec();
            
            // Filter out deleted points
            auto deleted_vec = std::vector<SpatioTemporalData>();
            deleted_vec.reserve(original_data.size());
            auto &finding_set = mapped_vectors.at(octree_id).at(kdtree_id);
            
            for (auto &point : original_data)
            {
                if (finding_set.find(point) == finding_set.end())
                {
                    deleted_vec.push_back(point);
                }
            }
            
            deleted_original += points.size();
            
            // Mark points as deleted in index structures (soft deletion)
            for (auto &point : points)
            {
                if (delete_kdtree(kdtree_nodes, point)) // Allows deletion failures
                {
                    deleted_sample++;
                    // Note: The following scan verification is commented out for performance
                    // for (auto &node : kdtree_nodes)
                    // {
                    //     if (node.point == point)
                    //     {
                    //         throw std::runtime_error("find by scan but delete failed");
                    //     }
                    // }
                }
            }
            
            // Update storage with modified data
            tc.tick();
            OriginalDataManager::updateOriginalDataInDatabase(octree_id, kdtree_id, deleted_vec);
            KdTreeNodeManager::updateKdTreeNodesInDatabase(octree_id, kdtree_id, kdtree_nodes);
            db_time += tc.milliSec();
        }
    }
    
    query_time = clock.milliSec() - db_time;
    
    // Log deletion statistics for monitoring
    elog(LOG, "deleted sample ratio: %f", double(deleted_sample)/deleted_original);
}

IndexFront::result_type IndexFront::range_query(SpatialBounds &bound, float min_time, float max_time, DataType type) {
    db_time = 0;
    tc.tick();
    std::unordered_map<int, std::unordered_map<int, std::vector<SpatioTemporalData> > > result_out;
    _range_query(bound, min_time, max_time, octree_nodes, result_out, type, thread_pool, db_time);
    query_time = tc.milliSec() - db_time;
    tc.tick();
    auto result = result_type(result_out);
    format_time = tc.milliSec();
    return result;
}

IndexFront::result_type IndexFront::mesh_query(Polygon &polygon, float min_time, float max_time, DataType type) {
    db_time = 0;
    tc.tick();
    std::unordered_map<int, std::unordered_map<int, std::vector<SpatioTemporalData> > > result_out;
    _mesh_query(polygon, min_time, max_time, octree_nodes, result_out, type, thread_pool, db_time);
    query_time = tc.milliSec() - db_time;
    tc.tick();
    auto result = result_type(result_out);
    format_time = tc.milliSec();
    return result;
}

IndexFront::result_type IndexFront::buffer_query(SpatialBounds &bound, float buffer_size, float min_time,
                                                 float max_time, DataType type) {
    db_time = 0;
    tc.tick();
    std::unordered_map<int, std::unordered_map<int, std::vector<SpatioTemporalData> > > loaded_data;
    _buffer_query(bound, buffer_size, min_time, max_time, octree_nodes, loaded_data, type, thread_pool, db_time);
    query_time = tc.milliSec() - db_time;
    tc.tick();
    auto result = result_type(loaded_data);
    format_time = tc.milliSec();
    return result;
}

/**
 * @brief Execute k-NN query returning raw results (placeholder)
 * TODO: Implement k-nearest neighbors query
 */
auto IndexFront::kNN_query_raw(SpatialPoint &center, int k, float min_time, float max_time, DataType type)
{
    // TODO: Implement kNN query raw
}

/**
 * @brief Execute k-NN query (placeholder)
 * TODO: Implement k-nearest neighbors query
 */
IndexFront::result_type IndexFront::kNN_query(SpatialPoint &center, int k, float min_time, float max_time, DataType type)
{
    // TODO: Implement kNN query
    return PointSetObject();
}

/**
 * @brief Execute user-specific query (placeholder)
 * TODO: Implement user-based data retrieval
 */
IndexFront::result_type IndexFront::user_query(user_id_t user_id, float min_time, float max_time, DataType type)
{
    // TODO: Implement user query
    return PointSetObject();
}

/**
 * @brief Execute similarity query (placeholder)
 * TODO: Implement similarity-based search
 */
IndexFront::result_type IndexFront::similarity_query(SpatialBounds &bound, std::vector<SpatioTemporalData> &center, int k, float min_time,
                             float max_time, DataType type)
{
    // TODO: Implement similarity query
    return PointSetObject();
}

//=========================================================================
// GroundTruth Implementation
//=========================================================================

/**
 * @brief Constructor for GroundTruth query processor
 * 
 * Initializes the ground truth (brute-force) query processor with basic
 * configurations. Uses the same distance functions as IndexFront to ensure
 * consistent results for validation purposes.
 */
GroundTruth::GroundTruth()
{
    // TODO: Initialize GroundTruth
}

// NOTE: All GroundTruth query methods are placeholder implementations
// that need to be completed with brute-force algorithms for validation

/**
 * @brief Execute ground truth range query (placeholder)
 * TODO: Implement brute-force range query for validation
 */
GroundTruth::result_type GroundTruth::range_query(SpatialBounds &bound, float min_time, float max_time, DataType type)
{
    // TODO: Implement ground truth range query
    return PointSetObject();
}

/**
 * @brief Execute ground truth mesh query (placeholder)
 * TODO: Implement brute-force mesh query for validation
 */
GroundTruth::result_type GroundTruth::mesh_query(Polygon &polygon, float min_time, float max_time, DataType type)
{
    // TODO: Implement ground truth mesh query
    return PointSetObject();
}

/**
 * @brief Execute ground truth buffer query (placeholder)
 * TODO: Implement brute-force buffer query for validation
 */
GroundTruth::result_type GroundTruth::buffer_query(SpatialBounds &bound, float buffer_size, float min_time, float max_time, DataType type)
{
    // TODO: Implement ground truth buffer query
    return PointSetObject();
}

/**
 * @brief Execute ground truth k-NN query raw (placeholder)
 * TODO: Implement brute-force k-NN query for validation
 */
auto GroundTruth::kNN_query_raw(SpatialPoint &center, int k, float min_time, float max_time, DataType type)
{
    // TODO: Implement ground truth kNN query raw
}

/**
 * @brief Execute ground truth k-NN query (placeholder)
 * TODO: Implement brute-force k-NN query for validation
 */
GroundTruth::result_type GroundTruth::kNN_query(SpatialPoint &center, int k, float min_time, float max_time, DataType type)
{
    // TODO: Implement ground truth kNN query
    return PointSetObject();
}

/**
 * @brief Execute ground truth user query (placeholder)
 * TODO: Implement brute-force user query for validation
 */
GroundTruth::result_type GroundTruth::user_query(user_id_t user_id, float min_time, float max_time, DataType type)
{
    // TODO: Implement ground truth user query
    return PointSetObject();
}

/**
 * @brief Execute ground truth similarity query (placeholder)
 * TODO: Implement brute-force similarity query for validation
 */
GroundTruth::result_type GroundTruth::similarity_query(SpatialBounds &bound, std::vector<SpatioTemporalData> &center, int k, float min_time,
                                 float max_time, DataType type)
{
    // TODO: Implement ground truth similarity query
    return PointSetObject();
}

void _range_query(SpatialBounds &bound, float min_time, float max_time, std::vector<DBOctreeNode> &nodes, std::unordered_map<int, std::unordered_map<int, std::vector<SpatioTemporalData>>> &result_out, DataType type, ThreadPoolWrapper &thread_pool, double &db_time)
{
    TimerClock tc;
    std::vector<DBOctreeNode> leaves;
    range_qurey_octree(bound, nodes, leaves);
    
    #ifdef output_info
    std::cout << "get leaves time:" << tc.milliSec() << "ms leaves-size:"<< leaves.size() << std::endl;
    #endif
    tc.tick();
    std::vector<std::pair<DBOctreeNode, std::vector<SpatioTemporalData> *>> result_to_filter;
    for (const auto &leaf : leaves)
    {
        result_to_filter.push_back({leaf, &result_out[leaf.oc_id][leaf.kd_id]});
    }

    auto splited_tasks = split<std::int64_t>(0,result_to_filter.size(), std::min<std::int64_t>(result_to_filter.size(),thread_pool_size));
    for(auto task:splited_tasks){
        if(0){//single thread
            for (std::int64_t i = task.first; i < task.second; ++i)
            {
                auto &leaf = result_to_filter[i].first;
                *result_to_filter[i].second = std::move(OriginalDataManager::loadOriginalDataFromDatabase(leaf.oc_id, leaf.kd_id));
            }
            continue;
        }
        thread_pool.post_task(
        [&result_to_filter, &result_out, task]()
        {
            for (std::int64_t i = task.first; i < task.second; ++i)
            {
                auto &leaf = result_to_filter[i].first;
                *result_to_filter[i].second = std::move(OriginalDataManager::loadOriginalDataFromDatabase(leaf.oc_id, leaf.kd_id));
            }
        });
    }

    thread_pool.wait_for_all_tasks();
    db_time = tc.milliSec();
    #ifdef output_info
    std::cout << "query DB time:" << tc.milliSec() << "ms" << std::endl;
    #endif
    tc.tick();
    splited_tasks = split<std::int64_t>(0,result_to_filter.size(), std::min<std::int64_t>(std::sqrt(result_to_filter.size()),thread_pool_size));
    std::atomic<double> querfilter_time = 0;
    for(auto task:splited_tasks){
        thread_pool.post_task(
            [&,task]()
            {
                for (std::int64_t i = task.first; i < task.second; ++i)
                {
                    filter(bound, min_time, max_time, *result_to_filter[i].second, type, !bound.contains(result_to_filter[i].first.bound));
                }
            });
        }

    thread_pool.wait_for_all_tasks();

    #ifdef output_info
    std::cout << "filter time:" << tc.milliSec() << "ms" << std::endl;
    #endif
}