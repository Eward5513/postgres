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

#endif // OCTREE_NODE_MANAGER_H