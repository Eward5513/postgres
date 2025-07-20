#include "../include/octree_node.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <queue>
#include "../include/octree_node_manager.h"

extern "C" {
    #include "postgres.h"
    #include "utils/elog.h"
}

// Default constructor
OctreeNode::OctreeNode() : id(-1), pointCount(0), level(0), is_leaf(false) {}

// Parameterized constructor
OctreeNode::OctreeNode(const Bounds& b, int level) : bound(b), id(-1), pointCount(0), level(level), is_leaf(false) {
    for (int i = 0; i < 8; ++i) {
        children[i] = nullptr;
    }
}

// Destructor
OctreeNode::~OctreeNode() {
    for (auto& child : children) {
        if (child) {
            delete child;
            child = nullptr;
        }
    }
}

// Check if the node is a leaf
bool OctreeNode::isLeaf() const {
    return is_leaf;
}

// Convert the node to a leaf
void OctreeNode::convertToLeaf() {
    is_leaf = true;
    for (int i = 0; i < 8; i++) {
        if (children[i]) {
            delete children[i];
            children[i] = nullptr;
        }
    }
}

// Static function: Save the node to a file
void OctreeNode::saveNode(const OctreeNode* node, std::ofstream& out) {
    if (!node) return;
    if (node->id < 0) {
        puts("node->id < 0");
        throw std::runtime_error("node->id < 0");
    }

    // Save node basic information
    out.write(reinterpret_cast<const char*>(&node->id), sizeof(node->id));
    out.write(reinterpret_cast<const char*>(&node->pointCount), sizeof(node->pointCount));
    out.write(reinterpret_cast<const char*>(&node->level), sizeof(node->level));
    out.write(reinterpret_cast<const char*>(&node->is_leaf), sizeof(node->is_leaf));

    // Save bounding box
    out.write(reinterpret_cast<const char*>(&node->bound), sizeof(node->bound));

    // Save points indices
    int points_size = node->points.size();
    out.write(reinterpret_cast<const char*>(&points_size), sizeof(points_size));
    out.write(reinterpret_cast<const char*>(node->points.data()), points_size * sizeof(int));

    // Recursively save children
    for (int i = 0; i < 8; i++) {
        bool hasChild = (node->children[i] != nullptr);
        out.write(reinterpret_cast<const char*>(&hasChild), sizeof(hasChild));
        if (hasChild) {
            saveNode(node->children[i], out);
        }
    }
}

// Static function: Load the node from a file
OctreeNode* OctreeNode::loadNode(std::ifstream& in) {
    OctreeNode* node = new OctreeNode();

    // Load node basic information
    in.read(reinterpret_cast<char*>(&node->id), sizeof(node->id));
    in.read(reinterpret_cast<char*>(&node->pointCount), sizeof(node->pointCount));
    in.read(reinterpret_cast<char*>(&node->level), sizeof(node->level));
    in.read(reinterpret_cast<char*>(&node->is_leaf), sizeof(node->is_leaf));

    // Load bounding box
    in.read(reinterpret_cast<char*>(&node->bound), sizeof(node->bound));

    // Load points indices
    int points_size;
    in.read(reinterpret_cast<char*>(&points_size), sizeof(points_size));
    node->points.resize(points_size);
    in.read(reinterpret_cast<char*>(node->points.data()), points_size * sizeof(int));

    // Recursively load children
    for (int i = 0; i < 8; i++) {
        bool hasChild;
        in.read(reinterpret_cast<char*>(&hasChild), sizeof(hasChild));
        if (hasChild) {
            node->children[i] = loadNode(in);
        }
    }

    return node;
}

// Static function: Save the octree to a file
void OctreeNode::saveOctree(const OctreeNode* root, const std::string& filename) {
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open file for saving: " << filename << std::endl;
        return;
    }
    saveNode(root, out);
    out.close();
}

// Static function: Load the octree from a file
OctreeNode* OctreeNode::loadOctree(const std::string& filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Failed to open file for loading: " << filename << std::endl;
        return nullptr;
    }
    OctreeNode* root = loadNode(in);
    in.close();
    return root;
}


 void AddIdForOctreeNode(OctreeNode* node,long long &id_count) {
    if (!node) return;
    if(node->is_leaf && node->id < 0){
        puts("叶子结点的id小于零-理论上不存在");
    }
    if(node->id < 0){
        node->id = id_count++;
    }

    // Recursively copy children
    for (int i = 0; i < 8; ++i) {
        AddIdForOctreeNode(node->children[i],id_count);
    }
}

 OctreeNode* copyOctreeNode(const OctreeNode* source) {
    if (!source) return nullptr; // Handle null source

    // Allocate new node and copy data
    OctreeNode* newNode = new OctreeNode();
    newNode->bound = source->bound;
    newNode->points = source->points;
    newNode->id = source->id;
    newNode->pointCount = source->pointCount;
    newNode->level = source->level;
    newNode->is_leaf = source->is_leaf;

    // Recursively copy children
    for (int i = 0; i < 8; ++i) {
        newNode->children[i] = copyOctreeNode(source->children[i]);
    }

    return newNode;
}

 bool compareOctrees(const OctreeNode* node1, const OctreeNode* node2) {
    // 如果两个节点都是空的，则它们相同
    if (!node1 && !node2) return true;

    // 如果只有一个节点为空，则它们不同
    if (!node1 || !node2) return false;

    // 比较当前节点的 id 和 bound
    if (node1->id != node2->id || node1->bound != node2->bound) return false;

    // 递归比较每个子节点
    for (int i = 0; i < 8; i++) {
        if (!compareOctrees(node1->children[i], node2->children[i])) {
            return false;
        }
    }

    // 如果所有子节点都相同，则这两个节点相同
    return true;
}

// Function to recursively collect all leaf nodes
void getLeafNodes(OctreeNode* node, std::vector< OctreeNode*>& leaves) {
    if (!node) return;
    if (node->isLeaf()) {
        leaves.push_back(node); // Add current node if it is a leaf
    } else {
        for (int i = 0; i < 8; ++i) {
            getLeafNodes(node->children[i], leaves); // Recursively add children
        }
    }
}

/**
 * @brief Convert in-memory octree structure to database-compatible format
 * 
 * This function transforms a complete in-memory octree (represented as linked pointers)
 * into a flat array of database-compatible nodes suitable for binary storage in PostgreSQL.
 * 
 * CRITICAL DESIGN NOTE - Data vs Structure Separation:
 * This function deliberately ONLY converts the spatial structure (tree topology, bounds, 
 * node relationships) but does NOT preserve the actual point data (node->points arrays).
 * This is because:
 * 
 * 1. INDEX INVALIDATION: The node->points arrays contain indices pointing to a temporary
 *    dataPoints container that will be destroyed when the calling function ends.
 *    Storing these indices would create "dangling references" with no target data.
 * 
 * 2. DATA STORAGE STRATEGY: The actual spatiotemporal data is extracted and stored 
 *    separately through the KD-tree construction process (para_createTreeWithLeafNode)
 *    BEFORE this conversion occurs, while the dataPoints container is still valid.
 * 
 * 3. ROLE SEPARATION: After storage, the octree serves as a "spatial routing table"
 *    that guides queries to the correct KD-tree partition, rather than containing 
 *    the actual data points.
 * 
 * Conversion process:
 * 1. Flatten the tree: Convert pointer-based tree to flat array via depth-first traversal
 * 2. Structure preservation: Convert parent-child pointer relationships to ID references
 * 3. Spatial bounds: Copy bounding box information for spatial query optimization
 * 4. Leaf identification: Mark leaf nodes and associate them with KD-tree identifiers
 * 5. Index compatibility: Ensure all nodes can be accessed by array index (node ID)
 * 
 * Database storage format:
 * - Each DBOctreeNode contains spatial bounds, parent-child relationships via IDs
 * - Leaf nodes include associations to their corresponding KD-tree storage
 * - Tree topology is preserved through the children[8] array mapping octant indices to node IDs
 * - No point data is stored - this is handled by separate KD-tree and original data tables
 * 
 * @param root Pointer to the root node of the in-memory octree structure
 *             Must be a valid octree with nodes having sequential IDs assigned by encode_Octree()
 * @param chunk_id Unique identifier for the spatial chunk this octree represents
 *                 Used to associate leaf nodes with their corresponding data partitions
 * 
 * @return std::vector<DBOctreeNode> Flat array of database-compatible octree nodes
 *         - Indexed by node ID for O(1) access during queries
 *         - Contains complete tree structure for spatial navigation
 *         - Ready for binary serialization and PostgreSQL storage
 * 
 * @note This function must be called AFTER encode_Octree() assigns sequential IDs to all nodes
 * @note The original tree structure (root parameter) should be deleted after this call
 * @note Point data extraction must occur BEFORE calling this function while dataPoints is valid
 * @note Resulting DBOctreeNode array serves as spatial index only, not data storage
 * 
 * @see encode_Octree() for ID assignment
 * @see para_createTreeWithLeafNode() for actual data extraction and KD-tree creation
 * @see OctreeNodeManager::writeOctreeNodesToDatabase() for database persistence
 */
std::vector<DBOctreeNode> convertOctreeToDB(OctreeNode* root, int chunk_id) {
    elog(INFO, "convertOctreeToDB: Converting octree structure to database format for chunk %d", chunk_id);
    
    // Step 1: Flatten the pointer-based tree structure into a linear array
    // This traverses the entire tree and collects all nodes in a vector for processing
    std::vector<OctreeNode*> all_nodes;
    getAllNodes(root, all_nodes);
    elog(INFO, "convertOctreeToDB: Found %zu nodes in octree structure", all_nodes.size());
    
    // Step 2: Pre-allocate database node array with exact size for efficiency
    // Each in-memory node will have a corresponding database node at the same index
    std::vector<DBOctreeNode> dbNodes;
    dbNodes.resize(all_nodes.size());
    
    // Step 3: Convert each in-memory node to database format
    for(auto node : all_nodes) {
        DBOctreeNode dbNode;
        
        // Copy spatial bounding box from in-memory format to database format
        // This preserves the spatial boundaries for query intersection tests
        dbNode.bound.min.x = node->bound.min.x;
        dbNode.bound.min.y = node->bound.min.y;
        dbNode.bound.min.z = node->bound.min.z;
        dbNode.bound.max.x = node->bound.max.x;
        dbNode.bound.max.y = node->bound.max.y;
        dbNode.bound.max.z = node->bound.max.z;
        
        // Copy node identification and type information
        dbNode.id = node->id;                    // Node ID for array indexing
        dbNode.is_leaf = node->is_leaf;          // Leaf status for query termination
        
        // Special processing for leaf nodes: associate with data storage locations
        if(dbNode.is_leaf) {
            dbNode.oc_id = chunk_id;             // Chunk identifier for data partitioning
            dbNode.kd_id = node->id;             // KD-tree identifier for this leaf's data
            // NOTE: The actual point data for this leaf will be stored in a separate
            // KD-tree structure identified by (chunk_id, node->id) key pair
        }
        
        // Convert parent-child pointer relationships to ID-based references
        // This enables tree navigation after pointer information is lost in storage
        for(int i = 0; i < 8; ++i) {
            if(node->children[i] != nullptr) {
                // Store child node ID instead of pointer for database compatibility
                dbNode.children[i] = node->children[i]->id;
                // children[i] represents octant i in 3D space subdivision:
                // 0:[000] = (-x,-y,-z), 1:[001] = (-x,-y,+z), 2:[010] = (-x,+y,-z), ...
            }
            // Note: Uninitialized children remain -1 (no child in that octant)
        }
        
        // CRITICAL OMISSION - Why node->points is NOT copied:
        // The node->points vector contains indices [5, 12, 23, ...] pointing into
        // the temporary dataPoints container. Since dataPoints will be destroyed
        // when the calling function ends, storing these indices would create
        // meaningless "dangling references" that point to non-existent data.
        // Instead, the actual point data is extracted via these indices and stored
        // in KD-tree format BEFORE this conversion occurs.
        
        // Place the converted node in the array at its ID index for O(1) access
        dbNodes[dbNode.id] = dbNode;
    }
    
    elog(INFO, "convertOctreeToDB: Successfully converted %zu nodes to database format", dbNodes.size());
    return dbNodes;
}

bool validateConversion(const OctreeNode* originalNode, const std::vector<DBOctreeNode>& dbNodes, int dbIndex) {
    if (!originalNode && dbIndex == -1) return true;
    if (!originalNode || dbIndex == -1) return false;
    // std::cout <<<< std::endl;
    const DBOctreeNode& dbNode = dbNodes[dbIndex];

    // Check basic properties
    if (dbNode.id != originalNode->id ||
        dbNode.is_leaf != originalNode->is_leaf) {
        return false;
    }

    // Check children
    for (int i = 0; i < 8; ++i) {
        if (!validateConversion(originalNode->children[i], dbNodes, dbNode.children[i])) {
            return false;
        }
    }

    return true;
}

 void getAllNodes(OctreeNode* node, std::vector< OctreeNode*>& nodes) {
    if (!node) return;
    nodes.push_back(node); // Add current node to the list
    for (int i = 0; i < 8; ++i) {
        getAllNodes(node->children[i], nodes); // Recursively add children
    }
}


void encode_Octree(OctreeNode *node)
{
    int node_count = 0;
    std::queue<OctreeNode *> routes;
    routes.push(node);
    while (!routes.empty())
    {
        node = routes.front();
        routes.pop();
        node->id = node_count++;
        for (auto &child : node->children)
        {
            if (child)
            {
                routes.push(child);
            }
        }
    }
}

// OctreePointQuery::OctreePointQuery()
// {
//     chunk_tree = octreeNodeManager.loadOctreeNodesFromDatabase(-1);
// }