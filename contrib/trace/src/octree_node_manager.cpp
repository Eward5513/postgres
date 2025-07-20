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

/**
 * @brief Perform range query on octree using spherical bounds (point + radius)
 * 
 * This function traverses an octree structure to find all leaf nodes that intersect
 * with a spherical query region defined by a center point and radius. It uses a
 * breadth-first search approach with spatial pruning to efficiently identify
 * relevant leaf nodes.
 * 
 * Algorithm Overview:
 * 1. Start from octree root node (index 0)
 * 2. For each node, calculate minimum distance from center point to node bounds
 * 3. If distance ≤ radius: node potentially contains relevant data
 * 4. If leaf node: add to results
 * 5. If internal node: check if completely contained for bulk collection
 * 6. Otherwise: recursively check all valid children
 * 
 * Spatial Optimization:
 * - Early termination when node bounds are too far from query sphere
 * - Bulk collection when entire subtree is within query sphere
 * - Distance calculations avoid expensive square root operations when possible
 * 
 * @param center Central point of the spherical query region
 * @param radius Search radius from the center point
 * @param nodes Complete octree node array for traversal (input)
 * @param leaves Output vector to collect matching leaf nodes (output)
 * 
 * @note Function modifies 'leaves' vector by appending matching nodes
 * @note Assumes nodes[0] is the root of the octree structure
 * @note Uses pointToBoundsDistance() and pointToBoundsMaxDistance() for spatial calculations
 * 
 * @warning nodes vector must contain valid octree structure with proper parent-child relationships
 * @warning radius should be positive; negative radius may produce unexpected results
 */
void range_qurey_octree(SpatialPoint &center, float radius,std::vector<DBOctreeNode> &nodes,std::vector<DBOctreeNode> &leaves){
    // Initialize BFS queue with root node (index 0)
    std::queue<int> node_ids;
    node_ids.push(0);
    
    // Breadth-first traversal of octree structure
    while(!node_ids.empty()){
        // Process next node in queue
        auto node_id = node_ids.front();
        node_ids.pop();
        auto &node = nodes[node_id];
        
        // SPATIAL PRUNING: Check if node bounds intersect with query sphere
        // Use minimum distance from point to bounding box
        if(pointToBoundsDistance(center,node.bound) <= radius){
            // LEAF NODE: Direct match - add to results
            if(node.is_leaf){
                leaves.push_back(node);
                continue;
            }
            
            // OPTIMIZATION: Check if entire subtree is contained within query sphere
            // If maximum distance from center to any point in bounds ≤ radius,
            // then all descendants are guaranteed to be within query region
            if(pointToBoundsMaxDistance(center,node.bound)<=radius){
                // Bulk collect all leaves in this subtree
                get_leaves_octree(node.id,nodes,leaves);
                continue;
            }
            
            // PARTIAL OVERLAP: Some children may be relevant, continue traversal
            // Add all valid children to queue for further processing
            for(int i = 0;i<8;++i){
                if(node.children[i]!=-1){
                    node_ids.push(node.children[i]);
                }
            }
        }
        // SPATIAL PRUNING: Node bounds too far from query sphere - skip entire subtree
    }
}

/**
 * @brief Perform range query on octree using rectangular bounds
 * 
 * This function traverses an octree structure to find all leaf nodes that intersect
 * with a rectangular query region (bounding box). It employs spatial pruning
 * techniques to efficiently navigate the tree and collect relevant leaf nodes.
 * 
 * Algorithm Overview:
 * 1. Start from octree root node (index 0)
 * 2. For each node, test intersection between node bounds and query bounds
 * 3. If intersection exists: node potentially contains relevant data
 * 4. If leaf node: add to results
 * 5. If query bounds completely contain node bounds: bulk collect entire subtree
 * 6. Otherwise: recursively check all valid children for partial overlaps
 * 
 * Spatial Optimization:
 * - Early termination when node bounds don't intersect query bounds
 * - Bulk collection when query bounds completely contain node bounds
 * - Efficient bounding box intersection tests
 * 
 * @param bound Rectangular query region (bounding box)
 * @param nodes Complete octree node array for traversal (input)
 * @param leaves Output vector to collect matching leaf nodes (output)
 * 
 * @note Function modifies 'leaves' vector by appending matching nodes
 * @note Assumes nodes[0] is the root of the octree structure
 * @note Uses SpatialBounds.intersects() and SpatialBounds.contains() for spatial tests
 * 
 * @warning nodes vector must contain valid octree structure
 * @warning bound should be a valid bounding box (min ≤ max for all dimensions)
 */
void range_qurey_octree(SpatialBounds &bound,std::vector<DBOctreeNode> &nodes,std::vector<DBOctreeNode> &leaves){
    // Initialize BFS queue with root node (index 0)
    std::queue<int> node_ids;
    node_ids.push(0);
    
    // Breadth-first traversal of octree structure
    while(!node_ids.empty()){
        // Process next node in queue
        auto node_id = node_ids.front();
        node_ids.pop();
        auto &node = nodes[node_id];
        
        // SPATIAL PRUNING: Check if node bounds intersect with query bounds
        if(node.bound.intersects(bound)){
            // LEAF NODE: Direct match - add to results
            if(node.is_leaf){
                leaves.push_back(node);
                continue;
            }
            
            // OPTIMIZATION: Check if query bounds completely contain node bounds
            // If so, all descendants are guaranteed to be within query region
            if(bound.contains(node.bound)){
                // Bulk collect all leaves in this subtree
                get_leaves_octree(node.id,nodes,leaves);
                continue;
            }
            
            // PARTIAL OVERLAP: Some children may be relevant, continue traversal
            // Add all valid children to queue for further processing
            for(int i = 0;i<8;++i){
                if(node.children[i]!=-1){
                    node_ids.push(node.children[i]);
                }
            }
        }
        // SPATIAL PRUNING: Node bounds don't intersect query bounds - skip entire subtree
    }
}

/**
 * @brief Collect all leaf nodes in a subtree rooted at specified node
 * 
 * This function performs a complete traversal of an octree subtree starting from
 * a given node ID and collects all leaf nodes encountered. It uses breadth-first
 * search to ensure systematic traversal and avoid stack overflow issues.
 * 
 * Algorithm Overview:
 * 1. Start from specified input node ID
 * 2. Use BFS to traverse all reachable nodes in the subtree
 * 3. For each leaf node encountered: add to results collection
 * 4. For each internal node: add all valid children to traversal queue
 * 5. Continue until all nodes in subtree have been processed
 * 
 * Use Cases:
 * - Bulk collection when entire subtree matches query criteria
 * - Complete enumeration of data in a spatial region
 * - Subtree analysis and statistics gathering
 * 
 * @param input_id Root node ID of the subtree to traverse
 * @param nodes Complete octree node array for traversal (input)
 * @param leaves Output vector to collect all leaf nodes in subtree (output)
 * 
 * @note Function modifies 'leaves' vector by appending all leaf nodes found
 * @note Uses breadth-first search to avoid potential stack overflow with deep trees
 * @note Handles invalid child references (-1) gracefully by skipping them
 * 
 * @warning input_id must be a valid index in the nodes array
 * @warning nodes vector must contain valid octree structure with proper parent-child relationships
 * @warning Function does not validate input_id bounds - caller responsibility
 */
void get_leaves_octree(int input_id, std::vector<DBOctreeNode> &nodes, std::vector<DBOctreeNode> &leaves){
    // Initialize BFS queue with specified root node
    std::queue<int> node_ids;
    node_ids.push(input_id);
    
    // Breadth-first traversal of subtree
    while(!node_ids.empty()){
        // Process next node in queue
        auto node_id = node_ids.front();
        node_ids.pop();
        auto &node = nodes[node_id];
        
        // LEAF NODE: Add to results collection
        if(node.is_leaf){
            leaves.push_back(node);
            continue;
        }
        
        // INTERNAL NODE: Add all valid children to queue for further processing
        // Iterate through all 8 potential children (octree property)
        for(int i = 0;i<8;++i){
            // Check if child exists (valid reference, not -1)
            if(node.children[i]!=-1){
                node_ids.push(node.children[i]);
            }
        }
    }
}

/**
 * @brief Recursively build octree structure using spatial subdivision
 * 
 * This function implements the core octree construction algorithm using a recursive
 * top-down approach. It spatially partitions data points into octants and creates
 * tree nodes until termination conditions are met.
 * 
 * Algorithm Overview:
 * 1. Check termination condition (point count ≤ leaf size limit)
 * 2. If terminating: create leaf node and store point indices
 * 3. If continuing: create internal node and partition space into 8 octants
 * 4. Distribute points to octants based on spatial coordinates
 * 5. Recursively build subtrees for each octant
 * 6. Return constructed node with all children attached
 * 
 * Spatial Partitioning Strategy:
 * - Uses binary subdivision along each axis (x, y, z)
 * - Each octant is identified by a 3-bit index (0-7)
 * - Bit 0: X-axis (0=left, 1=right)
 * - Bit 1: Y-axis (0=bottom, 1=top) 
 * - Bit 2: Z-axis (0=front, 1=back)
 * 
 * @param bound Spatial bounding box for the current octree node
 * @param pointIndices Vector of indices referencing dataPoints to be subdivided
 * 
 * @return Pointer to newly created OctreeNode with complete subtree structure
 * 
 * @note Uses move semantics for leaf nodes to avoid copying point indices
 * @note Creates child nodes even if empty to maintain octree structure completeness
 * @note Memory allocated with 'new' - caller responsible for proper cleanup
 * 
 * @warning Recursive function - stack depth limited by spatial subdivision levels
 * @warning Thread safety depends on dataPoints container being read-only during construction
 */
OctreeNode *OctreeBuilder::buildOctreeRecursive(const Bounds &bound, std::vector<int> &pointIndices)
{
    // TERMINATION CONDITION: Create leaf node if point count is within limit
    if (pointIndices.size() <= leafSizeLimit)
    {
        // Create leaf node with current spatial bounds
        auto *node = new OctreeNode(bound);
        node->is_leaf = true;
        
        // Transfer point indices using move semantics for efficiency
        // After this operation, pointIndices becomes empty
        node->points = std::move(pointIndices);
        
        return node;
    }
    
    // INTERNAL NODE CREATION: Continue spatial subdivision
    auto *node = new OctreeNode(bound);
    node->is_leaf = false;
    
    // Extract boundary coordinates for spatial calculations
    const auto &min = bound.min;
    const auto &max = bound.max;
    
    // Calculate center point for 3D space partitioning
    SpatioTemporalData center = bound.getCenter();
    
    // Initialize 8 child containers for octant-based point distribution
    // Index mapping: [000, 001, 010, 011, 100, 101, 110, 111] (binary)
    std::vector<int> childIndices[8];
    
    // POINT DISTRIBUTION: Assign each point to appropriate octant
    for (int idx : pointIndices)
    {
        const auto &point = dataPoints[idx];
        
        // Calculate 3-bit octant index using spatial comparisons
        uint64_t childIndex = 0;
        
        // X-axis subdivision (bit 0)
        if (point.x < center.x)
        {
            // Left half: bit 0 remains 0
        }
        else
        {
            // Right half: set bit 0
            childIndex |= 1;
        }
        
        // Y-axis subdivision (bit 1)
        if (point.y < center.y)
        {
            // Bottom half: bit 1 remains 0
        }
        else
        {
            // Top half: set bit 1
            childIndex |= 2;
        }
        
        // Z-axis subdivision (bit 2)
        if (point.z < center.z)
        {
            // Front half: bit 2 remains 0
        }
        else
        {
            // Back half: set bit 2
            childIndex |= 4;
        }
        
        // Assign point index to corresponding octant
        childIndices[childIndex].push_back(idx);
    }
    
    // RECURSIVE SUBDIVISION: Create child nodes for all 8 octants
    for (int i = 0; i < 8; ++i)
    {
        // Note: Creating all children even if empty to maintain complete octree structure
        // This simplifies traversal algorithms and ensures predictable tree topology
        
        // Calculate child boundary using bit manipulation
        SpatioTemporalData childMin = min;
        SpatioTemporalData childMax = max;
        
        // X-axis boundary adjustment (bit 0)
        if (i & 1)
        {
            // Right octants: adjust minimum X to center
            childMin.x = center.x;
        }
        else
        {
            // Left octants: adjust maximum X to center
            childMax.x = center.x;
        }
        
        // Y-axis boundary adjustment (bit 1)
        if (i & 2)
        {
            // Top octants: adjust minimum Y to center
            childMin.y = center.y;
        }
        else
        {
            // Bottom octants: adjust maximum Y to center
            childMax.y = center.y;
        }
        
        // Z-axis boundary adjustment (bit 2)
        if (i & 4)
        {
            // Back octants: adjust minimum Z to center
            childMin.z = center.z;
        }
        else
        {
            // Front octants: adjust maximum Z to center
            childMax.z = center.z;
        }
        
        // Construct child boundary and recursively build subtree
        Bounds childBound = {childMin, childMax};
        node->children[i] = buildOctreeRecursive(childBound, childIndices[i]);
    }
    
    return node;
}