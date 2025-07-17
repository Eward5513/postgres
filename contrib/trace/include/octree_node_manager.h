#ifndef OCTREE_NODE_MANAGER_H
#define OCTREE_NODE_MANAGER_H

#include <vector>
#include <unordered_map>
#include <numeric>
#include "octree_node.h"

/**
 * @brief Manager class for octree node database operations using singleton pattern
 * 
 * This class provides database operations for storing and retrieving octree nodes
 * in PostgreSQL database. It uses singleton pattern to ensure global unique instance
 * and provides thread-safe operations for octree data management.
 * 
 * Features:
 * - Singleton pattern for global access
 * - Binary data storage and retrieval
 * - Batch operations for efficiency
 * - Data validation and error handling
 */
class OctreeNodeManager {
public:
    static constexpr const char* TABLE_NAME = "all_octree_table";
    
    /**
     * @brief Get singleton instance
     * @return OctreeNodeManager& singleton reference
     */
    static OctreeNodeManager& getInstance();
    
    // Delete copy constructor and assignment operations
    OctreeNodeManager(const OctreeNodeManager&) = delete;
    OctreeNodeManager& operator=(const OctreeNodeManager&) = delete;
    OctreeNodeManager(OctreeNodeManager&&) = delete;
    OctreeNodeManager& operator=(OctreeNodeManager&&) = delete;
    
    /**
     * @brief Clear all data from octree table
     * 
     * Truncates the octree table to remove all existing data.
     * This is typically called before loading new octree data.
     */
    void clearTable();

    /**
     * @brief Write octree nodes to database
     * 
     * Stores a vector of octree nodes as binary data in the database
     * associated with the given key.
     * 
     * @param key Unique identifier for the octree chunk
     * @param dbNodes Vector of octree nodes to store
     */
    void writeOctreeNodesToDatabase(int key, const std::vector<DBOctreeNode>& dbNodes);

    /**
     * @brief Load octree nodes from database by key
     * 
     * Retrieves and deserializes octree nodes associated with the given key.
     * 
     * @param key Unique identifier for the octree chunk
     * @return std::vector<DBOctreeNode> Vector of loaded octree nodes
     * @throws PostgreSQL error if data not found or corrupted
     */
    std::vector<DBOctreeNode> loadOctreeNodesFromDatabase(int key);

    /**
     * @brief Load all octree nodes from database
     * 
     * Retrieves all octree data from the database and returns them
     * organized by their keys.
     * 
     * @return std::unordered_map<int, std::vector<DBOctreeNode>> Map of key to octree nodes
     */
    std::unordered_map<int, std::vector<DBOctreeNode>> loadAllOctreeNodesFromDatabase();

    /**
     * @brief Validate conversion between in-memory and database formats
     * 
     * Validates that the conversion between OctreeNode and DBOctreeNode
     * maintains data integrity.
     * 
     * @param originalNode Pointer to original in-memory octree node
     * @param dbNodes Vector of database octree nodes
     * @param dbIndex Index of the node to validate in dbNodes
     * @return bool True if validation passes, false otherwise
     */
    bool validateConversion(const OctreeNode* originalNode, const std::vector<DBOctreeNode>& dbNodes, int dbIndex);


    std::vector<DBOctreeNode>& getOctreeNodeByKey(int key);
private:
    /**
     * @brief Private constructor for singleton pattern
     */
    OctreeNodeManager() = default;
    
    /**
     * @brief Private destructor for singleton pattern
     */
    ~OctreeNodeManager() = default;

    std::unordered_map<int, std::vector<DBOctreeNode>> octree_nodes;
};

class OctreeBuilder
{
public:
    OctreeBuilder(size_t leafSizeLimit) : leafSizeLimit(leafSizeLimit) {}

    OctreeNode *buildOctree(const Bounds &bound)
    {
        std::vector<int> pointIndices(dataPoints.size());
        std::iota(pointIndices.begin(), pointIndices.end(), 0); // 初始化数据点索引
        return buildOctreeRecursive(bound, pointIndices);
    }

    std::vector<SpatioTemporalData> dataPoints;
    size_t leafSizeLimit;

private:
    OctreeNode *buildOctreeRecursive(const Bounds &bound, std::vector<int> &pointIndices)
    {
        if (pointIndices.size() <= leafSizeLimit)
        {
            auto *node = new OctreeNode(bound);
            node->is_leaf = true;
            node->points = std::move(pointIndices);
            return node;
        }
        auto *node = new OctreeNode(bound);
        node->is_leaf = false;
        const auto &min = bound.min;
        const auto &max = bound.max;
        SpatioTemporalData center = bound.getCenter();
        std::vector<int> childIndices[8];

        for (int idx : pointIndices)
        {
            const auto &point = dataPoints[idx];
            uint64_t childIndex = 0;
            if (point.x < center.x)
            {
            }
            else
            {
                childIndex |= 1;
            }
            if (point.y < center.y)
            {
            }
            else
            {
                childIndex |= 2;
            }
            if (point.z < center.z)
            {
            }
            else
            {
                childIndex |= 4;
            }
            childIndices[childIndex].push_back(idx);
        }

        for (int i = 0; i < 8; ++i)
        {
            // if (!childIndices[i].empty())
            {
                SpatioTemporalData childMin = min;
                SpatioTemporalData childMax = max;

                if (i & 1)
                    childMin.x = center.x;
                else
                    childMax.x = center.x;
                if (i & 2)
                    childMin.y = center.y;
                else
                    childMax.y = center.y;
                if (i & 4)
                    childMin.z = center.z;
                else
                    childMax.z = center.z;

                Bounds childBound = {childMin, childMax};
                node->children[i] = buildOctreeRecursive(childBound, childIndices[i]);
            }
        }
        return node;
    }
};

/**
 * @brief Global singleton instance of OctreeNodeManager
 * 
 * This provides convenient global access to the octree node manager instance
 * throughout the application, similar to pgutils for database operations.
 */
extern OctreeNodeManager& octreeNodeManager;

#endif // OCTREE_NODE_MANAGER_H