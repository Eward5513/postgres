#ifndef KDTREE_NODE_MANAGER_H
#define KDTREE_NODE_MANAGER_H

#include <vector>
#include <unordered_map>
#include <atomic>
#include "octree_node.h"

/**
 * @brief Database representation of a KD-Tree node
 * 
 * This structure represents a KD-Tree node optimized for database storage.
 * It contains all necessary information to reconstruct the tree structure
 * and perform spatial queries efficiently.
 */
struct DBKdtreeNode {
    SpatioTemporalData point;                            // The point stored in this node
    int id = -1;                                         // Unique identifier for this node
    int left_child = -1;                                 // ID of left child node (-1 if none)
    int right_child = -1;                                // ID of right child node (-1 if none)
    uint8_t division_axis = -1;                          // Splitting axis (0:x, 1:y, 2:z)
};

/**
 * @brief Validate the structural integrity of a KD-Tree
 * 
 * Performs recursive validation to ensure the KD-Tree maintains proper
 * ordering properties. Each node should have left children with values
 * less than or equal to the node's value on the splitting axis, and
 * right children with values greater than the node's value.
 * 
 * @param id Root node ID to start validation from
 * @param kdnodes Vector containing all KD-Tree nodes
 * @return bool True if the tree structure is valid, false otherwise
 */
bool check_kdtree(int id, std::vector<DBKdtreeNode> &kdnodes);

/**
 * @brief Build a KD-Tree from a collection of spatial points
 * 
 * Constructs a balanced KD-Tree using recursive median splitting.
 * The algorithm chooses the splitting axis based on the dimension
 * with the largest spread of values for optimal tree balance.
 * 
 * @param points Vector of spatial-temporal data points to build tree from
 * @return std::vector<DBKdtreeNode> Array of nodes representing the complete tree
 */
std::vector<DBKdtreeNode> buildKdTree(std::vector<SpatioTemporalData>& points);

/**
 * @brief Manager class for KD-Tree node database operations using singleton pattern
 * 
 * This class provides comprehensive database operations for storing and retrieving
 * KD-Tree nodes in PostgreSQL database. It uses singleton pattern to ensure global
 * unique instance and provides thread-safe operations for KD-Tree data management.
 * 
 * Key features:
 * - Singleton pattern for global access and consistency
 * - Dual-key indexing for efficient spatial partitioning
 * - Binary data storage for optimal performance
 * - Batch operations for large-scale data handling
 * - Data validation and integrity checking
 * - Comprehensive error handling and logging
 * 
 * Database schema:
 * - Table: all_kdtree
 * - Primary key: (key1, key2) - dual key for spatial/temporal partitioning
 * - Data: Binary serialized DBKdtreeNode arrays
 * 
 * Usage patterns:
 * - key1: typically represents spatial chunk ID
 * - key2: typically represents temporal segment or sub-chunk ID
 */

 /**
 * @brief In-memory representation of a KD-Tree node
 * 
 * This structure represents a KD-Tree node in memory with explicit pointers
 * to child nodes. Used during tree construction and traversal operations
 * before conversion to database format.
 */
struct KdtreeNode {
    SpatioTemporalData point;  // The point stored in this node
    KdtreeNode* left;          // Pointer to left child node
    KdtreeNode* right;         // Pointer to right child node
    uint8_t axis;              // Splitting axis (0: x, 1: y, 2: z)
    int id;                    // Unique node identifier

    /**
     * @brief Constructor for KD-Tree node
     * 
     * @param pt The spatial-temporal point to store in this node
     * @param split_axis The axis used for splitting (0: x, 1: y, 2: z)
     * @param node_id Unique identifier for this node
     */
    KdtreeNode(const SpatioTemporalData& pt, uint8_t split_axis, int node_id)
        : point(pt), left(nullptr), right(nullptr), axis(split_axis), id(node_id) {}
};

class KdTreeNodeManager {
public:
    static constexpr const char* TABLE_NAME = "all_kdtree";
    
    /**
     * @brief Get singleton instance of KdTreeNodeManager
     * 
     * Implements thread-safe lazy initialization using static local variable.
     * The first call creates the instance, subsequent calls return the same instance.
     * 
     * @return KdTreeNodeManager& Reference to the singleton instance
     * 
     * @note Thread-safe in C++11 and later due to static local variable initialization
     * @note Memory is automatically managed by the runtime
     */
    static KdTreeNodeManager& getInstance();
    
    // Delete copy constructor and assignment operations to enforce singleton
    KdTreeNodeManager(const KdTreeNodeManager&) = delete;
    KdTreeNodeManager& operator=(const KdTreeNodeManager&) = delete;
    KdTreeNodeManager(KdTreeNodeManager&&) = delete;
    KdTreeNodeManager& operator=(KdTreeNodeManager&&) = delete;
    
    /**
     * @brief Clear all data from the KD-Tree table
     * 
     * Truncates the entire KD-Tree table to remove all existing data.
     * This operation is typically performed before loading new KD-Tree structures
     * to ensure a clean state and avoid data conflicts.
     * 
     * Database operation:
     * - Executes TRUNCATE TABLE command for complete data removal
     * - More efficient than DELETE for removing all rows
     * - Resets any auto-increment counters
     * - Cannot be rolled back in most database configurations
     * 
     * @note This operation is irreversible - all KD-Tree data will be lost
     * @note Logs the operation for debugging and monitoring purposes
     */
    void clearTable();

    /**
     * @brief Write KD-Tree nodes to database with dual-key indexing
     * 
     * Stores a collection of KD-Tree nodes in the database using binary serialization.
     * The nodes are associated with dual keys (key1, key2) for efficient spatial
     * and temporal partitioning and later retrieval.
     * 
     * Storage format:
     * - Key1: Primary spatial partition identifier (e.g., chunk ID)
     * - Key2: Secondary partition identifier (e.g., temporal segment)
     * - Data: Binary serialized array of DBKdtreeNode structures
     * - Size: Total byte size of the serialized data
     * 
     * @param key1 Primary key identifier for spatial partitioning
     * @param key2 Secondary key identifier for temporal or sub-spatial partitioning
     * @param dbNodes Vector of KD-Tree nodes in database-compatible format
     * 
     * @note Uses PostgreSQL binary insert interface for efficient large data storage
     * @note Key combination (key1, key2) should be unique to avoid data conflicts
     * @note Nodes are stored as contiguous binary array for optimal I/O performance
     * @note Automatically validates data integrity before storage
     */
    void writeKdTreeNodesToDatabase(int key1, int key2, const std::vector<DBKdtreeNode>& dbNodes);

    /**
     * @brief Update existing KD-Tree nodes in database using upsert operation
     * 
     * Updates existing KD-Tree data or inserts new data if not found, using
     * PostgreSQL's UPSERT mechanism (INSERT ... ON CONFLICT ... DO UPDATE).
     * This provides atomic update operations for concurrent access scenarios.
     * 
     * Operation behavior:
     * - If (key1, key2) exists: updates the existing binary data
     * - If (key1, key2) doesn't exist: inserts new data entry
     * - Atomic operation prevents race conditions in multi-threaded environments
     * 
     * @param key1 Primary key identifier for spatial partitioning
     * @param key2 Secondary key identifier for temporal or sub-spatial partitioning
     * @param updatedNodes Vector of updated KD-Tree nodes
     * 
     * @note Thread-safe operation suitable for concurrent data updates
     * @note More efficient than separate delete/insert operations
     * @note Maintains referential integrity during updates
     */
    void updateKdTreeNodesInDatabase(int key1, int key2, const std::vector<DBKdtreeNode>& updatedNodes);

    /**
     * @brief Load KD-Tree nodes from database by dual-key lookup
     * 
     * Retrieves and deserializes KD-Tree nodes associated with the specified
     * dual keys (key1, key2). The binary data is validated for integrity
     * and converted back to the in-memory KD-Tree node format.
     * 
     * Retrieval process:
     * 1. Query database for binary data using dual keys
     * 2. Validate data size matches expected structure size
     * 3. Deserialize binary data into DBKdtreeNode objects
     * 4. Perform structural validation if enabled
     * 5. Return vector of reconstructed nodes
     * 
     * @param key1 Primary key identifier for spatial partitioning
     * @param key2 Secondary key identifier for temporal or sub-spatial partitioning
     * @return std::vector<DBKdtreeNode> Vector of loaded KD-Tree nodes
     * 
     * @throws std::runtime_error if data not found for specified keys
     * @throws std::runtime_error if binary data size is corrupted or invalid
     * @throws std::runtime_error if structural validation fails
     * 
     * @note Memory for the result is automatically managed by the vector
     * @note Validates data integrity before returning results
     * @note Returns empty vector if no data found (unless strict mode enabled)
     */
    std::vector<DBKdtreeNode> loadKdTreeNodesFromDatabase(int key1, int key2);

    /**
     * @brief Load all KD-Tree nodes from database organized by dual keys
     * 
     * Retrieves all KD-Tree data from the database and organizes it into a
     * nested map structure for efficient access by dual keys. This method
     * is useful for bulk operations or when working with multiple KD-Tree
     * partitions simultaneously.
     * 
     * Processing workflow:
     * 1. Query all binary data from the KD-Tree table
     * 2. Validate each data chunk for size consistency
     * 3. Deserialize valid chunks into KD-Tree node vectors
     * 4. Organize results by dual keys in nested unordered maps
     * 5. Handle corrupted or empty data gracefully
     * 
     * @return std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> 
     *         Nested map: key1 -> key2 -> vector of KD-Tree nodes
     * 
     * @note Empty vectors are created for key combinations with no valid data
     * @note Corrupted data entries are skipped with warning logs
     * @note Memory management is handled automatically for all allocations
     * @note Returns empty map if no data exists in the database
     * @note May consume significant memory for large datasets
     */
    std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> loadAllKdTreeNodes();

    /**
     * @brief Load KD-Tree nodes from database by primary key
     * 
     * Retrieves all KD-Tree data associated with a specific primary key (key1)
     * and organizes it by secondary keys (key2). This method is efficient for
     * loading all temporal segments or sub-partitions of a spatial chunk.
     * 
     * Use cases:
     * - Loading all temporal segments of a spatial chunk
     * - Bulk operations on spatially related KD-Trees
     * - Efficient batch processing of related partitions
     * 
     * @param key1 Primary key identifier to filter by
     * @return std::unordered_map<int, std::vector<DBKdtreeNode>> 
     *         Map: key2 -> vector of KD-Tree nodes
     * 
     * @throws std::runtime_error if no data found for specified key1
     * @throws std::runtime_error if data corruption detected
     * 
     * @note More efficient than loading all data when only specific key1 needed
     * @note Validates data integrity for each loaded partition
     */
    std::unordered_map<int, std::vector<DBKdtreeNode>> loadKdTreeNodesFromDatabase(int key1);

    /**
     * @brief Load KD-Tree nodes from database for specific key pairs
     * 
     * Retrieves KD-Tree data for a specified list of key pairs, allowing
     * efficient batch loading of specific partitions without loading all data.
     * This method is optimal for targeted queries on known key combinations.
     * 
     * Batch processing benefits:
     * - Reduced database round trips compared to individual queries
     * - Efficient memory usage by loading only required partitions
     * - Maintains data consistency across related partitions
     * 
     * @param keyPairs Vector of key pairs to load data for
     * @return std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> 
     *         Nested map: key1 -> key2 -> vector of KD-Tree nodes
     * 
     * @throws std::runtime_error if keyPairs vector is empty
     * @throws std::runtime_error if data corruption detected for any key pair
     * 
     * @note Key pairs with no data are excluded from the result
     * @note More efficient than loadAllKdTreeNodes() for sparse queries
     * @note Validates each loaded partition independently
     */
    std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> loadKdTreeNodesFromDatabase(
        const std::vector<std::pair<int, int>>& keyPairs);

    /**
     * @brief Validate conversion between in-memory and database KD-Tree formats
     * 
     * Performs comprehensive validation to ensure that the conversion between
     * KdtreeNode (in-memory format) and DBKdtreeNode (database format)
     * preserves data integrity and structural consistency.
     * 
     * Validation checks:
     * 1. Node ID consistency between formats
     * 2. Point data preservation (coordinates and attributes)
     * 3. Tree structure integrity (parent-child relationships)
     * 4. Splitting axis correctness
     * 5. Recursive validation of all subtrees
     * 
     * @param originalNode Pointer to original in-memory KD-Tree node (can be null)
     * @param dbNodes Vector containing all database KD-Tree nodes
     * @param dbIndex Index of the current node to validate in dbNodes (-1 for null)
     * @return bool True if validation passes, false if any inconsistency found
     * 
     * @note Handles null nodes gracefully for sparse tree structures
     * @note Recursively validates entire subtrees for complete verification
     * @note Performance scales with tree size and depth
     * @note Essential for debugging data corruption issues
     */
    bool validateConversion(const KdtreeNode* originalNode, const std::vector<DBKdtreeNode>& dbNodes, int dbIndex);

private:
    /**
     * @brief Private constructor for singleton pattern
     * 
     * Prevents direct instantiation of the class, enforcing singleton behavior.
     * Initialization is performed automatically when getInstance() is first called.
     */
    KdTreeNodeManager() = default;
    
    /**
     * @brief Private destructor for singleton pattern
     * 
     * Handles cleanup of any resources when the application terminates.
     * Called automatically by the runtime for static instance cleanup.
     */
    ~KdTreeNodeManager() = default;
};

/**
 * @brief Builder class for constructing KD-Trees from spatial data
 * 
 * This class provides functionality to build balanced KD-Trees from collections
 * of spatial-temporal data points. It handles memory management, tree balancing,
 * and conversion between in-memory and database formats.
 * 
 * Key features:
 * - Automatic tree balancing using median splitting
 * - Optimal axis selection based on data distribution
 * - Memory management for tree nodes
 * - Conversion to database-compatible format
 * - Thread-safe operations (non-copyable by design)
 */
class KDTreeBuilder {
private:
    int id_count = 0;                                    // Counter for assigning node IDs
    KdtreeNode* root = nullptr;                          // Root node of the constructed tree

    /**
     * @brief Find the optimal splitting axis for a range of points
     * 
     * Analyzes the distribution of points across all three spatial dimensions
     * and selects the axis with the largest spread for optimal tree balance.
     * 
     * @param points Vector of spatial-temporal points
     * @param start Starting index of the range to analyze
     * @param end Ending index of the range to analyze (exclusive)
     * @return uint8_t Optimal splitting axis (0: x, 1: y, 2: z)
     */
    uint8_t findDivisionAxis(const std::vector<SpatioTemporalData>& points, int start, int end);

    /**
     * @brief Recursively build KD-Tree from a range of points
     * 
     * Constructs a balanced KD-Tree using recursive median splitting.
     * The algorithm partitions points around the median value of the
     * selected splitting axis.
     * 
     * @param points Vector of spatial-temporal points (modified during construction)
     * @param start Starting index of the range to process
     * @param end Ending index of the range to process (exclusive)
     * @param depth Current depth in the tree (for debugging/logging)
     * @return KdtreeNode* Pointer to the root of the constructed subtree
     */
    KdtreeNode* buildRecursive(std::vector<SpatioTemporalData>& points, int start, int end, int depth);

    /**
     * @brief Recursively delete all nodes in a KD-Tree
     * 
     * Performs post-order traversal to safely delete all nodes in the tree,
     * ensuring proper memory cleanup.
     * 
     * @param node Root node of the subtree to delete
     */
    void deleteTree(KdtreeNode* node);

    /**
     * @brief Convert in-memory KD-Tree to database format
     * 
     * Recursively converts the in-memory tree structure to a flat array
     * of database-compatible nodes, maintaining parent-child relationships
     * through ID references.
     * 
     * @param node Current node to convert
     * @param depth Current depth in the tree (for debugging)
     * @param result Vector to store the converted nodes
     */
    void convert_to_db_type(const KdtreeNode* node, int depth, std::vector<DBKdtreeNode>& result);

public:
    /**
     * @brief Default constructor
     */
    KDTreeBuilder() = default;

    // Delete copy constructor and assignment operations
    KDTreeBuilder(const KDTreeBuilder&) = delete;
    KDTreeBuilder& operator=(const KDTreeBuilder&) = delete;

    /**
     * @brief Destructor - automatically cleans up tree memory
     */
    ~KDTreeBuilder();

    /**
     * @brief Build a KD-Tree from a collection of spatial points
     * 
     * Constructs a balanced KD-Tree from the input points. The root node
     * is guaranteed to have ID 0 for consistency.
     * 
     * @param points Vector of spatial-temporal points to build tree from
     * @throws std::runtime_error if root node ID is not 0
     */
    void buildKdTree(std::vector<SpatioTemporalData>& points);

    /**
     * @brief Get the root node of the constructed tree
     * 
     * @return const KdtreeNode* Pointer to the root node (null if empty tree)
     */
    const KdtreeNode* getRoot() const;

    /**
     * @brief Convert the in-memory tree to database format
     * 
     * Converts the entire tree structure to a flat array of database-compatible
     * nodes, suitable for storage in PostgreSQL.
     * 
     * @return std::vector<DBKdtreeNode> Array of database-compatible nodes
     */
    std::vector<DBKdtreeNode> convert();
};

/**
 * @brief Global atomic counter for finished octree operations
 * 
 * Used for monitoring and synchronization of octree processing operations
 * across multiple threads. Provides thread-safe access to operation status.
 */
inline std::atomic<long long> finihsed_octree(0);

/**
 * @brief Global atomic counter for index loading operations
 * 
 * Tracks the number of index loading operations in progress or completed.
 * Used for performance monitoring and load balancing.
 */
inline std::atomic<long long> index_loading_num(0);

/**
 * @brief Global singleton instance of KdTreeNodeManager
 * 
 * This provides convenient global access to the KD-Tree node manager instance
 * throughout the application, similar to pgutils for database operations.
 * 
 * Usage:
 * ```cpp
 * kdTreeNodeManager.writeKdTreeNodesToDatabase(key1, key2, nodes);
 * auto nodes = kdTreeNodeManager.loadKdTreeNodesFromDatabase(key1, key2);
 * ```
 */
extern KdTreeNodeManager& kdTreeNodeManager;

// KD-tree utility functions
bool insert_kdtree(std::vector<DBKdtreeNode> &kd_tree_nodes, const SpatioTemporalData &point);
bool delete_kdtree(std::vector<DBKdtreeNode> &kd_tree_nodes, const SpatioTemporalData &point);
bool delete_kdtree2(std::vector<DBKdtreeNode> &kd_tree_nodes, const SpatioTemporalData &point);
std::vector<DBKdtreeNode> rebuild_kdtree(std::vector<DBKdtreeNode> &kd_tree_nodes);

#endif // KDTREE_NODE_MANAGER_H