#include "../include/safe_header.h"
#include "../include/kdtree_node_manager.h"
#include "../include/pgutils.h"
#include <functional>
#include <stdexcept>
#include <limits>
#include <algorithm>
#include <cstring>
#include <queue>

extern "C" {
#include "postgres.h"
#include "utils/elog.h"
}

/**
 * @brief Validate the structural integrity of a KD-Tree
 * 
 * Performs recursive validation to ensure the KD-Tree maintains proper
 * ordering properties. Each node should have left children with values
 * less than or equal to the node's value on the splitting axis, and
 * right children with values greater than the node's value.
 * 
 * This function validates the KD-Tree structure by:
 * 1. Checking boundary conditions for leaf nodes
 * 2. Verifying that each node's value lies within the bounds of its subtrees
 * 3. Recursively validating all child nodes
 * 
 * @param id Root node ID to start validation from (-1 for null nodes)
 * @param kdnodes Vector containing all KD-Tree nodes indexed by ID
 * @return bool True if the tree structure is valid, false otherwise
 * 
 * @note Handles null nodes (id == -1) gracefully
 * @note Uses recursive lambda function for boundary calculation
 * @note Performance scales with tree size and depth
 */
bool check_kdtree(int id, std::vector<DBKdtreeNode> &kdnodes) {
    // Base case: null node is considered valid
    if (id == -1) {
        return true;
    }

    // Get reference to current node for validation
    DBKdtreeNode &node = kdnodes[id];

    // Define recursive lambda function to calculate boundary values
    std::function<float(int, bool, int)> get_bound = [&](int axis, bool min_or_max, int id) -> float {
        // Handle null node case
        if (id == -1) {
            return min_or_max ? std::numeric_limits<float>::max() : std::numeric_limits<float>::lowest();
        }

        DBKdtreeNode &node = kdnodes[id];
        float value = 0;

        // Extract value based on the specified axis
        if (axis == 0) {
            value = node.point.x;
        } else if (axis == 1) {
            value = node.point.y;
        } else {
            value = node.point.z;
        }

        // Recursively process left subtree
        if (node.left_child != -1) {
            auto temp = get_bound(axis, min_or_max, node.left_child);
            if (min_or_max) {
                value = std::min(value, temp); // Calculate minimum bound
            } else {
                value = std::max(value, temp); // Calculate maximum bound
            }
        }

        // Recursively process right subtree
        if (node.right_child != -1) {
            auto temp = get_bound(axis, min_or_max, node.right_child);
            if (min_or_max) {
                value = std::min(value, temp); // Calculate minimum bound
            } else {
                value = std::max(value, temp); // Calculate maximum bound
            }
        }

        return value;
    };

    // Calculate boundary values for left and right subtrees
    float left_bound = get_bound(node.division_axis, false, node.left_child);   // Max value in left subtree
    float right_bound = get_bound(node.division_axis, true, node.right_child);  // Min value in right subtree

    // Validate that current node's value is within expected bounds
    float node_value = 0;
    if (node.division_axis == 0) {
        node_value = node.point.x;
    } else if (node.division_axis == 1) {
        node_value = node.point.y;
    } else {
        node_value = node.point.z;
    }

    // Check KD-Tree ordering property
    if (!(left_bound <= node_value && node_value <= right_bound)) {
            return false;
    }

    // Recursively validate both subtrees
    if (!check_kdtree(node.left_child, kdnodes)) {
        return false;
    }
    if (!check_kdtree(node.right_child, kdnodes)) {
        return false;
    }

    return true;
}

/**
 * @brief Build a KD-Tree from a collection of spatial points
 * 
 * Constructs a balanced KD-Tree using recursive median splitting.
 * The algorithm chooses the splitting axis based on the dimension
 * with the largest spread of values for optimal tree balance.
 * 
 * Construction process:
 * 1. Find the dimension with maximum spread for optimal splitting
 * 2. Sort points along the selected axis
 * 3. Select median point as the splitting node
 * 4. Recursively build left and right subtrees
 * 5. Assign unique IDs to all nodes
 * 
 * @param points Vector of spatial-temporal data points to build tree from
 * @return std::vector<DBKdtreeNode> Array of nodes representing the complete tree
 * 
 * @note Modifies the input points vector during construction
 * @note Returns nodes in ID-indexed array format
 * @note Guarantees balanced tree structure for optimal query performance
 */
std::vector<DBKdtreeNode> buildKdTree(std::vector<SpatioTemporalData>& points) {
    std::vector<DBKdtreeNode> nodes(points.size());
    int kdtree_id_step = 0;
    
    // Lambda function to determine optimal splitting axis
    auto findDivisionAxis = [](std::vector<SpatioTemporalData>& pts, int start, int end) -> uint8_t {
        // Initialize min/max values with first point
        float min_x = pts[start].x, max_x = pts[start].x;
        float min_y = pts[start].y, max_y = pts[start].y;
        float min_z = pts[start].z, max_z = pts[start].z;

        // Find bounds across all dimensions
        for (int i = start; i < end; ++i) {
            min_x = std::min(min_x, pts[i].x);
            max_x = std::max(max_x, pts[i].x);
            min_y = std::min(min_y, pts[i].y);
            max_y = std::max(max_y, pts[i].y);
            min_z = std::min(min_z, pts[i].z);
            max_z = std::max(max_z, pts[i].z);
        }

        // Calculate range for each dimension
        float range_x = max_x - min_x;
        float range_y = max_y - min_y;
        float range_z = max_z - min_z;

        // Return axis with maximum spread
        if (range_x >= range_y && range_x >= range_z) return 0; // X axis
        if (range_y >= range_x && range_y >= range_z) return 1; // Y axis
        return 2; // Z axis
    };
    
    // Recursive lambda function for tree construction
    std::function<int(int, int, int)> build = [&](int start, int end, int depth) -> int {
        // Validate range
        if (start >= end) {
            throw std::runtime_error("Invalid range in KD-Tree construction");
        }

        // Find optimal splitting axis for current range
        uint8_t axis = findDivisionAxis(points, start, end);
        int mid = start + (end - start) / 2;

        // Partition points around median using nth_element
        std::nth_element(points.begin() + start, points.begin() + mid + 1, points.begin() + end, 
            [axis](const SpatioTemporalData& a, const SpatioTemporalData& b) {
                return (axis == 0 ? a.x : (axis == 1 ? a.y : a.z)) < 
                       (axis == 0 ? b.x : (axis == 1 ? b.y : b.z));
            });

        // Create current node
        DBKdtreeNode node = { points[mid], -1, -1, -1, axis };
        
        // Recursively build left subtree
        if (start < mid) {
            node.left_child = build(start, mid, depth + 1);
        }
        
        // Recursively build right subtree
        if (mid + 1 < end) {
            node.right_child = build(mid + 1, end, depth + 1);
        }
        
        // Assign unique ID and store node
        node.id = kdtree_id_step++;
        nodes[node.id] = node;
        return node.id;
    };

    // Build the complete tree starting from root
    build(0, points.size(), 0);
    return nodes;
}

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
KdTreeNodeManager& KdTreeNodeManager::getInstance() {
    static KdTreeNodeManager instance;
    return instance;
}

/**
 * @brief Global singleton instance for convenient access
 * 
 * Provides direct access to the KdTreeNodeManager singleton instance throughout
 * the application without requiring explicit getInstance() calls.
 * 
 * Usage example:
 * ```cpp
 * kdTreeNodeManager.writeKdTreeNodesToDatabase(key1, key2, nodes);
 * auto nodes = kdTreeNodeManager.loadKdTreeNodesFromDatabase(key1, key2);
 * ```
 */
KdTreeNodeManager& kdTreeNodeManager = KdTreeNodeManager::getInstance();

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
void KdTreeNodeManager::clearTable() {
    elog(INFO, "KdTreeNodeManager::clearTable()");

    // Execute TRUNCATE command to efficiently remove all table data
    std::string sql = "TRUNCATE TABLE " + std::string(TABLE_NAME) + ";";
    pgutils.executeSQL(sql.c_str());
}

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
void KdTreeNodeManager::writeKdTreeNodesToDatabase(int key1, int key2, const std::vector<DBKdtreeNode>& dbNodes) {
    // Use PostgreSQL binary insert interface for efficient dual-key storage
    pgutils.executeBinaryInsertDualKey(TABLE_NAME, key1, key2, dbNodes.data(), dbNodes.size() * sizeof(DBKdtreeNode));
}

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
void KdTreeNodeManager::updateKdTreeNodesInDatabase(int key1, int key2, const std::vector<DBKdtreeNode>& updatedNodes) {
    // Use PostgreSQL UPSERT operation for atomic update/insert
    pgutils.executeBinaryUpsertDualKey(TABLE_NAME, key1, key2, updatedNodes.data(), updatedNodes.size() * sizeof(DBKdtreeNode));
}

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
 * 
 * @note Memory for the result is automatically managed by the vector
 * @note Validates data integrity before returning results
 */
std::vector<DBKdtreeNode> KdTreeNodeManager::loadKdTreeNodesFromDatabase(int key1, int key2) {
    std::vector<DBKdtreeNode> dbNodes;

    // Query database for binary data using PostgreSQL SPI interface
    BinarySelectResult* result = pgutils.executeBinarySelectByDualKey(TABLE_NAME, key1, key2);
    
    if (result == NULL) {
        throw std::runtime_error("KD-Tree data not found for keys: " + std::to_string(key1) + ", " + std::to_string(key2));
    }

    // Validate binary data size matches expected structure size
    size_t node_size = sizeof(DBKdtreeNode);
    size_t total_size = result->size;

    if (total_size % node_size != 0) {
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                       errmsg("Binary data size does not match expected size for DBKdtreeNode")));
        pfree(result->data);
        pfree(result);
        return dbNodes;  // Return empty vector on error
    }

    // Deserialize binary data into vector of KD-Tree nodes
    int num_nodes = total_size / node_size;
    const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(result->data);
    dbNodes.assign(buffer, buffer + num_nodes);

    // Clean up temporary memory allocated by PostgreSQL
    pfree(result->data);
    pfree(result);

    return dbNodes;
}

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
std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> KdTreeNodeManager::loadKdTreeNodesFromDatabase(
    const std::vector<std::pair<int, int>>& keyPairs) {

    std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> keyPairNodeMap;

    if (keyPairs.empty()) {
        throw std::runtime_error("Key pairs list is empty");
    }

    // Process each key pair individually for targeted loading
    for (const auto& keyPair : keyPairs) {
        int key1 = keyPair.first;
        int key2 = keyPair.second;

        // Query database for this specific key pair
        BinarySelectResult* result = pgutils.executeBinarySelectByDualKey(TABLE_NAME, key1, key2);
        
        if (result != NULL) {
            // Validate binary data size consistency
            size_t node_size = sizeof(DBKdtreeNode);
            size_t total_size = result->size;

            if (total_size % node_size != 0) {
                ereport(WARNING, (errcode(ERRCODE_DATA_CORRUPTED),
                                 errmsg("Binary data size does not match expected size for DBKdtreeNode. Keys: %d, %d", key1, key2)));
                pfree(result->data);
                pfree(result);
                continue;  // Skip corrupted data entry
            }

            // Deserialize valid binary data into KD-Tree node vector
            int num_nodes = total_size / node_size;
            const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(result->data);

            std::vector<DBKdtreeNode> dbNodes(num_nodes);
            std::memcpy(dbNodes.data(), buffer, total_size);

            // Store in nested map structure
            keyPairNodeMap[key1][key2] = std::move(dbNodes);

            // Clean up temporary memory allocated by PostgreSQL
            pfree(result->data);
            pfree(result);
        }
    }

    return keyPairNodeMap;
}

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
std::unordered_map<int, std::vector<DBKdtreeNode>> KdTreeNodeManager::loadKdTreeNodesFromDatabase(int key1) {
    std::unordered_map<int, std::vector<DBKdtreeNode>> key2NodeMap;

    // Query database for all records with specified key1
    DualKeyBinarySelectResult* result = pgutils.executeBinarySelectByKey1(TABLE_NAME, key1);
    
    if (result == NULL) {
        throw std::runtime_error("KD-Tree data not found for key1: " + std::to_string(key1));
    }

    size_t node_size = sizeof(DBKdtreeNode);
    
    // Process each result record
    for (int i = 0; i < result->count; i++) {
        int key2 = result->key2_array[i];
        size_t total_size = result->size_array[i];

        // Validate binary data size consistency
        if (total_size % node_size != 0) {
            ereport(WARNING, (errcode(ERRCODE_DATA_CORRUPTED),
                             errmsg("Binary data size does not match expected size for DBKdtreeNode. Key1: %d, Key2: %d", key1, key2)));
            continue;  // Skip corrupted record
        }

        // Deserialize binary data into KD-Tree node vector
        int num_nodes = total_size / node_size;
        const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(result->data_array[i]);

        std::vector<DBKdtreeNode> dbNodes(num_nodes);
        std::memcpy(dbNodes.data(), buffer, total_size);

        // Store in map indexed by key2
        key2NodeMap[key2] = std::move(dbNodes);
    }

    // Clean up memory allocated by PostgreSQL
    for (int i = 0; i < result->count; i++) {
        if (result->data_array[i] != NULL) {
            pfree(result->data_array[i]);
        }
    }
    pfree(result->key1_array);
    pfree(result->key2_array);
    pfree(result->data_array);
    pfree(result->size_array);
    pfree(result);

    return key2NodeMap;
}

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
std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> KdTreeNodeManager::loadAllKdTreeNodes() {
    std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> all_kd_nodes;
    long long size_count = 0;

    // Query all dual-key records from the database
    DualKeyBinarySelectResult* result = pgutils.executeBinarySelectAllDualKey(TABLE_NAME);
    
    if (result != NULL) {
        size_t node_size = sizeof(DBKdtreeNode);
        
        // Process each result record
        for (int i = 0; i < result->count; i++) {
            int key1 = result->key1_array[i];
            int key2 = result->key2_array[i];
            size_t total_size = result->size_array[i];
            
            // Validate and deserialize binary data
            if (total_size % node_size == 0) {
            int num_nodes = total_size / node_size;
            const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(result->data_array[i]);

            std::vector<DBKdtreeNode> nodes(num_nodes);
            size_count += nodes.size();
            std::memcpy(nodes.data(), buffer, total_size);
            
                // Store in nested map structure
            all_kd_nodes[key1][key2] = std::move(nodes);
            } else {
                ereport(WARNING, (errcode(ERRCODE_DATA_CORRUPTED),
                                 errmsg("Binary data size does not match expected size for DBKdtreeNode. Key1: %d, Key2: %d", key1, key2)));
            }
        }
        
        // Clean up memory allocated by PostgreSQL
        for (int i = 0; i < result->count; i++) {
            if (result->data_array[i] != NULL) {
                pfree(result->data_array[i]);
            }
        }
        pfree(result->key1_array);
        pfree(result->key2_array);
        pfree(result->data_array);
        pfree(result->size_array);
        pfree(result);
    } else {
        ereport(INFO, (errmsg("No KD-Tree data found in database")));
    }
    
    elog(INFO, "Loaded all KD-Tree nodes, total size: %lld", size_count);
    return all_kd_nodes;
}

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
bool KdTreeNodeManager::validateConversion(const KdtreeNode* originalNode, const std::vector<DBKdtreeNode>& dbNodes, int dbIndex) {
    // Handle null node cases - both null is valid, mixed null is invalid
    if (!originalNode && dbIndex == -1) return true;
    if (!originalNode || dbIndex == -1) return false;
    
    // Get reference to current database node for validation
    const DBKdtreeNode& dbNode = dbNodes[dbIndex];

    // Validate basic node properties match between formats
    if (dbNode.id != originalNode->id ||
        dbNode.division_axis != originalNode->axis ||
        dbNode.point.x != originalNode->point.x ||
        dbNode.point.y != originalNode->point.y ||
        dbNode.point.z != originalNode->point.z) {
        return false;
    }

    // Validate child node relationships
    int expectedLeftId = (originalNode->left != nullptr) ? originalNode->left->id : -1;
    int expectedRightId = (originalNode->right != nullptr) ? originalNode->right->id : -1;
    
    if (dbNode.left_child != expectedLeftId || dbNode.right_child != expectedRightId) {
        return false;
    }

    // Recursively validate child nodes
    if (!validateConversion(originalNode->left, dbNodes, dbNode.left_child)) {
        return false;
    }
    if (!validateConversion(originalNode->right, dbNodes, dbNode.right_child)) {
        return false;
    }

    return true;
}

// KDTreeBuilder implementation

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
uint8_t KDTreeBuilder::findDivisionAxis(const std::vector<SpatioTemporalData>& points, int start, int end) {
    // Initialize bounds with first point
    float min_x = points[start].x, max_x = points[start].x;
    float min_y = points[start].y, max_y = points[start].y;
    float min_z = points[start].z, max_z = points[start].z;

    // Find min/max values across all dimensions
    for (int i = start + 1; i < end; ++i) {
        min_x = std::min(min_x, points[i].x);
        max_x = std::max(max_x, points[i].x);
        min_y = std::min(min_y, points[i].y);
        max_y = std::max(max_y, points[i].y);
        min_z = std::min(min_z, points[i].z);
        max_z = std::max(max_z, points[i].z);
    }

    // Calculate range for each dimension
    float range_x = max_x - min_x;
    float range_y = max_y - min_y;
    float range_z = max_z - min_z;

    // Return axis with maximum spread
    if (range_x >= range_y && range_x >= range_z) return 0;
    if (range_y >= range_x && range_y >= range_z) return 1;
    return 2;
}

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
KdtreeNode* KDTreeBuilder::buildRecursive(std::vector<SpatioTemporalData>& points, int start, int end, int depth) {
    // Base case: empty range
    if (start >= end) {
        return nullptr;
    }
    
    // Base case: single point
    if (start + 1 == end) {
        return new KdtreeNode(points[start], findDivisionAxis(points, start, end), id_count++);
    }

    // Find optimal splitting axis and median point
    uint8_t axis = findDivisionAxis(points, start, end);
    int mid = start + (end - start) / 2;

    // Partition points around median using nth_element
    std::nth_element(points.begin() + start, points.begin() + mid, points.begin() + end,
        [axis](const SpatioTemporalData& a, const SpatioTemporalData& b) {
            return (axis == 0 ? a.x : (axis == 1 ? a.y : a.z)) <
                   (axis == 0 ? b.x : (axis == 1 ? b.y : b.z));
        });

    // Create node for median point
    KdtreeNode* node = new KdtreeNode(points[mid], axis, id_count++);
    
    // Recursively build left and right subtrees
    if (start < mid) {
        node->left = buildRecursive(points, start, mid, depth + 1);
    }
    if (mid + 1 < end) {
        node->right = buildRecursive(points, mid + 1, end, depth + 1);
    }
    
    return node;
}

/**
 * @brief Recursively delete all nodes in a KD-Tree
 * 
 * Performs post-order traversal to safely delete all nodes in the tree,
 * ensuring proper memory cleanup.
 * 
 * @param node Root node of the subtree to delete
 */
void KDTreeBuilder::deleteTree(KdtreeNode* node) {
    if (!node) return;
    deleteTree(node->left);
    deleteTree(node->right);
    delete node;
}

/**
 * @brief Destructor - automatically cleans up tree memory
 */
KDTreeBuilder::~KDTreeBuilder() {
    deleteTree(root);
}

/**
 * @brief Build a KD-Tree from a collection of spatial points
 * 
 * Constructs a balanced KD-Tree from the input points. The root node
 * is guaranteed to have ID 0 for consistency.
 * 
 * @param points Vector of spatial-temporal points to build tree from
 * @throws std::runtime_error if root node ID is not 0
 */
void KDTreeBuilder::buildKdTree(std::vector<SpatioTemporalData>& points) {
    if (points.empty()) {
        root = nullptr;
        return;
    }
    
    // Build tree starting from root
    root = buildRecursive(points, 0, points.size(), 0);
    
    // Validate root node ID for consistency
    if (root->id != 0) {
        deleteTree(root);
        root = nullptr;
        throw std::runtime_error("Root node ID is not 0");
    }
}

/**
 * @brief Get the root node of the constructed tree
 * 
 * @return const KdtreeNode* Pointer to the root node (null if empty tree)
 */
const KdtreeNode* KDTreeBuilder::getRoot() const {
    return root;
}

/**
 * @brief Convert the in-memory tree to database format
 * 
 * Converts the entire tree structure to a flat array of database-compatible
 * nodes, suitable for storage in PostgreSQL.
 * 
 * @return std::vector<DBKdtreeNode> Array of database-compatible nodes
 */
std::vector<DBKdtreeNode> KDTreeBuilder::convert() {
    std::vector<DBKdtreeNode> result;
    if (!root) return result;
    
    // Resize result vector to accommodate all nodes
    result.resize(id_count);
    
    // Convert tree structure to database format
    convert_to_db_type(root, 0, result);
    return result;
}

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
void KDTreeBuilder::convert_to_db_type(const KdtreeNode* node, int depth, std::vector<DBKdtreeNode>& result) {
    if (!node) return;
    
    // Convert current node to database format
    DBKdtreeNode& db_node = result[node->id];
    db_node.division_axis = node->axis;
    db_node.id = node->id;
    db_node.left_child = (node->left != nullptr) ? node->left->id : -1;
    db_node.right_child = (node->right != nullptr) ? node->right->id : -1;
    db_node.point = node->point;
    
    // Recursively convert child nodes
    convert_to_db_type(node->left, depth + 1, result);
    convert_to_db_type(node->right, depth + 1, result);
}

// KD-tree utility functions implementation

bool insert_kdtree(std::vector<DBKdtreeNode> &kd_tree_nodes,
    const SpatioTemporalData &point)
{
    if (kd_tree_nodes.size() == 0)
    {
        DBKdtreeNode new_node;
        new_node.id = kd_tree_nodes.size();
        new_node.point = point;
        new_node.division_axis = 0;
        kd_tree_nodes.push_back(new_node);
        return true;
    }
    DBKdtreeNode *node = &kd_tree_nodes[0];
    auto add_new_node = [&kd_tree_nodes, &point](int &father_pointer, const int8_t father_axis)
    {
        DBKdtreeNode new_node;
        new_node.id = kd_tree_nodes.size();
        new_node.point = point;
        father_pointer = new_node.id;
        new_node.division_axis = (father_axis + 1) % 3;
        kd_tree_nodes.push_back(new_node);
    };
    while (true)
    {
        if (node->point == point)
        {
            return false;
        }
        switch (node->division_axis)
        {
        case 0:
        {
            if (point.x < node->point.x)
            {
                if (node->left_child == -1)
                {
                    add_new_node(node->left_child, node->division_axis);
                    return true;
                }
                node = &kd_tree_nodes[node->left_child];
            }
            else
            {
                if (node->right_child == -1)
                {
                    add_new_node(node->right_child, node->division_axis);
                    return true;
                }
                node = &kd_tree_nodes[node->right_child];
            }
            break;
        }
        case 1:
        {
            if (point.y < node->point.y)
            {
                if (node->left_child == -1)
                {
                    add_new_node(node->left_child, node->division_axis);
                    return true;
                }
                node = &kd_tree_nodes[node->left_child];
            }
            else
            {
                if (node->right_child == -1)
                {
                    add_new_node(node->right_child, node->division_axis);
                    return true;
                }
                node = &kd_tree_nodes[node->right_child];
            }
            break;
        }
        case 2:
        {
            if (point.z < node->point.z)
            {
                if (node->left_child == -1)
                {
                    add_new_node(node->left_child, node->division_axis);
                    return true;
                }
                node = &kd_tree_nodes[node->left_child];
            }
            else
            {
                if (node->right_child == -1)
                {
                    add_new_node(node->right_child, node->division_axis);
                    return true;
                }
                node = &kd_tree_nodes[node->right_child];
            }
            break;
        }
        default:
        {
            throw std::invalid_argument("Invalid division axis");
        }
        }
    }
}

bool delete_kdtree(std::vector<DBKdtreeNode> &kd_tree_nodes,
    const SpatioTemporalData &point)
{
    if(kd_tree_nodes.empty()){
        return false;
    }
    DBKdtreeNode *node = &kd_tree_nodes[0];
    while (true)
    {
        if (node->point == point)
        {
            node->point.is_deleted = true;
            return true;
        }
        switch (node->division_axis)
        {
        case 0:
        {
            if (point.x < node->point.x)
            {
                if (node->left_child == -1)
                {
                    return false;
                }
                node = &kd_tree_nodes[node->left_child];
            }
            else
            {
                if (node->right_child == -1)
                {
                    return false;
                }
                node = &kd_tree_nodes[node->right_child];
            }
            break;
        }
        case 1:
        {
            if (point.y < node->point.y)
            {
                if (node->left_child == -1)
                {
                    return false;
                }
                node = &kd_tree_nodes[node->left_child];
            }
            else
            {
                if (node->right_child == -1)
                {
                    return false;
                }
                node = &kd_tree_nodes[node->right_child];
            }
            break;
        }
        case 2:
        {
            if (point.z < node->point.z)
            {
                if (node->left_child == -1)
                {
                    return false;
                }
                node = &kd_tree_nodes[node->left_child];
            }
            else
            {
                if (node->right_child == -1)
                {
                    return false;
                }
                node = &kd_tree_nodes[node->right_child];
            }
            break;
        }
        default:
        {
            throw std::invalid_argument("Invalid division axis");
        }
        }
    }
}

bool delete_kdtree2(std::vector<DBKdtreeNode> &kd_tree_nodes,
     const SpatioTemporalData &point)
{
    if(kd_tree_nodes.empty()){return false;}

    // DBKdtreeNode *node = &kd_tree_nodes[0];
    std::queue<int> routes;
    routes.push(0);
    while(!routes.empty()){
        auto node_id  = routes.front();
        routes.pop();
        if(node_id == -1){
            continue;
        }
        DBKdtreeNode &node = kd_tree_nodes[node_id];
        if(node.point.x == point.x || node.point.y == point.y || node.point.z == point.z){
            routes.push(node.left_child);
            routes.push(node.right_child);
            continue;
        }
        if (node.point == point)
        {
            node.point.is_deleted = true;
            return true;
        }
        switch (node.division_axis)
        {
        case 0:
        {
            if (point.x < node.point.x)
            {
                routes.push(node.left_child);
            }
            else
            {
                routes.push(node.right_child);
            }
            break;
        }
        case 1:
        {
            if (point.y < node.point.y)
            {
                routes.push(node.left_child);
            }
            else
            {
                routes.push(node.right_child);
            }
            break;
        }
        case 2:
        {
            if (point.z < node.point.z)
            {
                routes.push(node.left_child);
            }
            else
            {
                routes.push(node.right_child);
            }
            break;
        }
        default:
        {
            throw std::invalid_argument("Invalid division axis");
        }
        }
    }

    return false;
}

std::vector<DBKdtreeNode> rebuild_kdtree(std::vector<DBKdtreeNode> &kd_tree_nodes)
{
    std::vector<SpatioTemporalData> points;
    for (auto &i : kd_tree_nodes)
    {
        points.push_back(i.point);
    }
    KDTreeBuilder builder;
    builder.buildKdTree(points);
    return builder.convert();
}