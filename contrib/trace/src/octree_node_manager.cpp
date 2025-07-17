#include "../include/safe_header.h"
#include "../include/octree_node_manager.h"
#include "../include/pgutils.h"
#include <cstring>

extern "C" {
#include "postgres.h"
#include "utils/elog.h"
}

/**
 * @brief Get singleton instance of OctreeNodeManager
 * 
 * Implements thread-safe lazy initialization using static local variable.
 * The first call creates the instance, subsequent calls return the same instance.
 * 
 * @return OctreeNodeManager& Reference to the singleton instance
 * 
 * @note Thread-safe in C++11 and later due to static local variable initialization
 * @note Memory is automatically managed by the runtime
 */
OctreeNodeManager& OctreeNodeManager::getInstance() {
    static OctreeNodeManager instance;
    return instance;
}

/**
 * @brief Global singleton instance for convenient access
 * 
 * Provides direct access to the OctreeNodeManager singleton instance throughout
 * the application without requiring explicit getInstance() calls.
 */
OctreeNodeManager& octreeNodeManager = OctreeNodeManager::getInstance();

/**
 * @brief Clear all data from the octree table
 * 
 * Truncates the entire octree table to remove all existing octree data.
 * This operation is typically performed before loading new octree structures
 * to ensure a clean state.
 * 
 * Database operation:
 * - Executes TRUNCATE TABLE command for complete data removal
 * - More efficient than DELETE for removing all rows
 * - Resets any auto-increment counters
 * 
 * @note This operation cannot be rolled back in some database configurations
 * @note Logs the operation for debugging and monitoring purposes
 */
void OctreeNodeManager::clearTable() {
    elog(INFO, "OctreeNodeManager::clearTable()");

    // Execute TRUNCATE command to efficiently remove all table data
    std::string sql = "TRUNCATE TABLE " + std::string(TABLE_NAME) + ";";
    pgutils.executeSQL(sql.c_str());
}

/**
 * @brief Write octree nodes to database as binary data
 * 
 * Stores a collection of octree nodes in the database using binary serialization.
 * The nodes are associated with a unique key identifier for later retrieval.
 * This method provides efficient storage for large octree structures.
 * 
 * Storage format:
 * - Key: Integer identifier for the octree chunk/partition
 * - Data: Binary serialized array of DBOctreeNode structures
 * - Size: Total byte size of the serialized data
 * 
 * @param key Unique identifier for the octree chunk or spatial partition
 * @param dbNodes Vector of octree nodes in database-compatible format
 * 
 * @note Uses PostgreSQL binary insert for efficient large data storage
 * @note Key should be unique to avoid overwriting existing data
 * @note Nodes are stored as a contiguous binary array for fast I/O
 */
void OctreeNodeManager::writeOctreeNodesToDatabase(int key, const std::vector<DBOctreeNode>& dbNodes) {
    // Use PostgreSQL binary insert interface for efficient storage
    octree_nodes[key] = dbNodes;
    pgutils.executeBinaryInsert(TABLE_NAME, key, dbNodes.data(), dbNodes.size() * sizeof(DBOctreeNode));
}

std::vector<DBOctreeNode>& OctreeNodeManager::getOctreeNodeByKey(int key) {
    return octree_nodes[key];
}

/**
 * @brief Load octree nodes from database by key identifier
 * 
 * Retrieves and deserializes octree nodes associated with the specified key.
 * The binary data is validated for integrity and converted back to the
 * in-memory octree node format.
 * 
 * Retrieval process:
 * 1. Query database for binary data using the key
 * 2. Validate data size matches expected structure size
 * 3. Deserialize binary data into DBOctreeNode objects
 * 4. Return vector of reconstructed nodes
 * 
 * @param key Unique identifier for the octree chunk to retrieve
 * @return std::vector<DBOctreeNode> Vector of loaded octree nodes
 * 
 * @throws PostgreSQL ERROR if data not found for the specified key
 * @throws PostgreSQL ERROR if binary data size is corrupted or invalid
 * 
 * @note Memory for the result is automatically managed by the vector
 * @note Validates data integrity before returning results
 */
std::vector<DBOctreeNode> OctreeNodeManager::loadOctreeNodesFromDatabase(int key) {
    std::vector<DBOctreeNode> dbNodes;
    
    // Query database for binary data using PostgreSQL SPI interface
    BinarySelectResult* result = pgutils.executeBinarySelect(TABLE_NAME, key);
    if (result != NULL) {
        // Validate binary data size matches expected structure size
        size_t node_size = sizeof(DBOctreeNode);
        if (result->size % node_size != 0) {
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                           errmsg("Binary data size does not match expected size for DBOctreeNode")));
        }
        
        // Deserialize binary data into vector of octree nodes
        int num_nodes = result->size / node_size;
        const DBOctreeNode* buffer = reinterpret_cast<const DBOctreeNode*>(result->data);
        dbNodes.assign(buffer, buffer + num_nodes);
        
        // Clean up temporary memory allocated by PostgreSQL
        pfree(result->data);
        pfree(result);
    } else {
        ereport(ERROR, (errcode(ERRCODE_NO_DATA_FOUND),
                       errmsg("Octree data not found for key: %d", key)));
    }
    
    return dbNodes;
}

/**
 * @brief Load all octree nodes from database organized by keys
 * 
 * Retrieves all octree data from the database and organizes it into a map
 * structure for efficient access by key. This method is useful for bulk
 * operations or when working with multiple octree partitions.
 * 
 * Processing workflow:
 * 1. Query all binary data from the octree table
 * 2. Validate each data chunk for size consistency
 * 3. Deserialize valid chunks into octree node vectors
 * 4. Organize results by key in an unordered map
 * 5. Handle corrupted or empty data gracefully
 * 
 * @return std::unordered_map<int, std::vector<DBOctreeNode>> Map from key to octree nodes
 * 
 * @note Empty vectors are created for keys with no valid data
 * @note Corrupted data entries are skipped with warning logs
 * @note Memory management is handled automatically for all allocations
 * @note Returns empty map if no data exists in the database
 */
std::unordered_map<int, std::vector<DBOctreeNode>> OctreeNodeManager::loadAllOctreeNodesFromDatabase() {
    std::unordered_map<int, std::vector<DBOctreeNode>> nodeMap;
    
    // Query all binary data from the octree table
    BinarySelectAllResult* result = pgutils.executeBinarySelectAll(TABLE_NAME);
    if (result != NULL) {
        // Process each key-value pair in the result set
        for (int i = 0; i < result->count; i++) {
            int key = result->keys[i];
            
            // Check if data exists and is non-empty for this key
            if (result->data_array[i] != NULL && result->size_array[i] > 0) {
                // Validate binary data size matches expected structure size
                size_t node_size = sizeof(DBOctreeNode);
                if (result->size_array[i] % node_size != 0) {
                    ereport(WARNING, (errcode(ERRCODE_DATA_CORRUPTED),
                                     errmsg("Binary data size does not match expected size for DBOctreeNode. Key: %d", key)));
                    continue;  // Skip corrupted data entries
                }
                
                // Deserialize valid binary data into octree node vector
                int num_nodes = result->size_array[i] / node_size;
                const DBOctreeNode* buffer = reinterpret_cast<const DBOctreeNode*>(result->data_array[i]);
                nodeMap[key].assign(buffer, buffer + num_nodes);
            } else {
                // Create empty vector for keys with no data
                nodeMap[key] = std::vector<DBOctreeNode>();
            }
        }
        
        // Clean up memory allocated by PostgreSQL for result data
        for (int i = 0; i < result->count; i++) {
            if (result->data_array[i] != NULL) {
                pfree(result->data_array[i]);
            }
        }
        pfree(result->keys);
        pfree(result->data_array);
        pfree(result->size_array);
        pfree(result);
    } else {
        ereport(WARNING, (errcode(ERRCODE_NO_DATA_FOUND),
                         errmsg("No octree data found in database")));
    }
    
    return nodeMap;
}

/**
 * @brief Validate conversion between in-memory and database octree formats
 * 
 * Performs recursive validation to ensure that the conversion between
 * OctreeNode (in-memory format) and DBOctreeNode (database format)
 * preserves data integrity and structural consistency.
 * 
 * Validation checks:
 * 1. Node ID consistency between formats
 * 2. Leaf node flag preservation
 * 3. Recursive validation of all child nodes
 * 4. Proper handling of null/empty nodes
 * 
 * Recursion handling:
 * - Base case: Both nodes are null (valid)
 * - Error case: Only one node is null (invalid)
 * - Recursive case: Validate current node and all children
 * 
 * @param originalNode Pointer to original in-memory octree node (can be null)
 * @param dbNodes Vector containing all database octree nodes
 * @param dbIndex Index of the current node to validate in dbNodes (-1 for null)
 * @return bool True if validation passes, false if any inconsistency found
 * 
 * @note Handles null nodes gracefully for sparse octree structures
 * @note Recursively validates entire subtrees for complete verification
 * @note Performance scales with octree size and depth
 */
bool OctreeNodeManager::validateConversion(const OctreeNode* originalNode, const std::vector<DBOctreeNode>& dbNodes, int dbIndex) {
    // Handle null node cases - both null is valid, mixed null is invalid
    if (!originalNode && dbIndex == -1) return true;
    if (!originalNode || dbIndex == -1) return false;
    
    // Get reference to current database node for validation
    const DBOctreeNode& dbNode = dbNodes[dbIndex];

    // Validate basic node properties match between formats
    if (dbNode.id != originalNode->id ||
        dbNode.is_leaf != originalNode->is_leaf) {
        return false;
    }

    // Recursively validate all child nodes for structural consistency
    for (int i = 0; i < 8; ++i) {
        if (!validateConversion(originalNode->children[i], dbNodes, dbNode.children[i])) {
            return false;
        }
    }

    return true;
}