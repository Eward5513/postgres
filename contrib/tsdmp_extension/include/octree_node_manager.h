#ifndef OCTREE_NODE_MANAGER_H
#define OCTREE_NODE_MANAGER_H

#include <vector>
#include <unordered_map>
#include "octree_node.h"

class OctreeNodeManager {
public:
    static constexpr const char* TABLE_NAME = "all_octree_table";
    
    static void clearTable();

    static void writeOctreeNodesToDatabase(int key, const std::vector<DBOctreeNode>& dbNodes);

    static std::vector<DBOctreeNode> loadOctreeNodesFromDatabase(int key);

    static std::unordered_map<int, std::vector<DBOctreeNode>> loadAllOctreeNodesFromDatabase();

private:
    OctreeNodeManager() = delete;
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

#endif // OCTREE_NODE_MANAGER_H