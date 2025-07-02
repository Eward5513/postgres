#ifndef KDTREE_NODE_MANAGER_H
#define KDTREE_NODE_MANAGER_H

#include <vector>
#include <unordered_map>
#include "octree_node.h"

class KdTreeNodeManager {
public:
    // 清空表数据，如果表不存在则创建
    static void clearTable();

    // 写入数据到数据库
    static void writeKdTreeNodesToDatabase(int key1, int key2, const std::vector<DBKdtreeNode>& dbNodes);
    static void updateKdTreeNodesInDatabase(int key1, int key2, const std::vector<DBKdtreeNode>& updatedNodes);

    // 从数据库读取数据
    static std::vector<DBKdtreeNode> loadKdTreeNodesFromDatabase(int key1, int key2);

    static std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> loadAllKdTreeNodes();

    static std::unordered_map<int, std::vector<DBKdtreeNode>> loadKdTreeNodesFromDatabase(int key1);
    static std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> loadKdTreeNodesFromDatabase(const std::vector<std::pair<int, int>>& keyPairs);

private:
    // 禁止直接创建对象
    KdTreeNodeManager() = delete;
};

#endif // KDTREE_NODE_MANAGER_H