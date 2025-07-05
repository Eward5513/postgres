#include "../include/safe_header.h"
#include "../include/kdtree_node_manager.h"
#include "../include/pgutils.h"
#include <functional>
#include <stdexcept>
#include <limits>
#include <algorithm>


bool check_kdtree(int id, std::vector<DBKdtreeNode> &kdnodes) {
    // 检查是否是有效节点
    if (id == -1) {
        return true;
    }

    DBKdtreeNode &node = kdnodes[id];

    // 定义 get_bound 闭包
    std::function<float(int, bool, int)> get_bound = [&](int axis, bool min_or_max, int id) -> float {
        if (id == -1) {
            return min_or_max ? std::numeric_limits<float>::max() : std::numeric_limits<float>::lowest();
        }

        DBKdtreeNode &node = kdnodes[id];
        float value = 0;

        // 获取当前节点在指定轴上的值
        if (axis == 0) {
            value = node.point.x;
        } else if (axis == 1) {
            value = node.point.y;
        } else {
            value = node.point.z;
        }

        // 递归处理左子树
        if (node.left_child != -1) {
            auto temp = get_bound(axis, min_or_max, node.left_child);
            if (min_or_max) {
                value = std::min(value, temp); // 计算最小值
            } else {
                value = std::max(value, temp); // 计算最大值
            }
        }

        // 递归处理右子树
        if (node.right_child != -1) {
            auto temp = get_bound(axis, min_or_max, node.right_child);
            if (min_or_max) {
                value = std::min(value, temp); // 计算最小值
            } else {
                value = std::max(value, temp); // 计算最大值
            }
        }

        return value;
    };

    // 计算左右子树的边界值
    float left_bound = get_bound(node.division_axis, false, node.left_child);
    float right_bound = get_bound(node.division_axis, true, node.right_child);

    // 检查当前节点是否在左右边界之间
    if (node.division_axis == 0) {
        if (!(left_bound <= node.point.x && node.point.x <= right_bound)) {
            return false;
        }
    } else if (node.division_axis == 1) {
        if (!(left_bound <= node.point.y && node.point.y <= right_bound)) {
            return false;
        }
    } else {
        if (!(left_bound <= node.point.z && node.point.z <= right_bound)) {
            return false;
        }
    }

    // 递归检查子树
    if (!check_kdtree(node.left_child, kdnodes)) {
        return false;
    }
    if (!check_kdtree(node.right_child, kdnodes)) {
        return false;
    }

    return true;
}

std::vector<DBKdtreeNode> buildKdTree(std::vector<SpatioTemporalData>& points) {
    std::vector<DBKdtreeNode> nodes(points.size());
    int kdtree_id_step = 0;
    auto findDivisionAxis = [](std::vector<SpatioTemporalData>& pts, int start, int end) -> uint8_t {
        float min_x = pts[start].x, max_x = pts[start].x;
        float min_y = pts[start].y, max_y = pts[start].y;
        float min_z = pts[start].z, max_z = pts[start].z;

        for (int i = start; i < end; ++i) {
            min_x = std::min(min_x, pts[i].x);
            max_x = std::max(max_x, pts[i].x);
            min_y = std::min(min_y, pts[i].y);
            max_y = std::max(max_y, pts[i].y);
            min_z = std::min(min_z, pts[i].z);
            max_z = std::max(max_z, pts[i].z);
        }

        float range_x = max_x - min_x;
        float range_y = max_y - min_y;
        float range_z = max_z - min_z;

        if (range_x >= range_y && range_x >= range_z) return 0; 
        if (range_y >= range_x && range_y >= range_z) return 1; 
        return 2; 
    };
    std::function<int(int, int, int)> build = [&](int start, int end, int depth) -> int {
        if (start >= end) {
            throw std::runtime_error("should not be here");
        }

        uint8_t axis = findDivisionAxis(points, start, end);
        int mid = start + (end - start) / 2;

        std::nth_element(points.begin() + start, points.begin() + mid + 1, points.begin() + end, 
            [axis](const SpatioTemporalData& a, const SpatioTemporalData& b) {
                return (axis == 0 ? a.x : (axis == 1 ? a.y : a.z)) < 
                       (axis == 0 ? b.x : (axis == 1 ? b.y : b.z));
            });

        DBKdtreeNode node = { points[mid], -1, -1, -1, axis };
        if(start < mid){
            node.left_child = build(start, mid, depth + 1);
        }
        if(mid + 1 < end){
            node.right_child = build(mid + 1, end, depth + 1);
        }
        node.id = kdtree_id_step++;
        nodes[node.id] = node;
        return node.id;
    };
    build(0, points.size(), 0);
    return nodes;
}

void KdTreeNodeManager::clearTable() {
    // 清理表中的数据
    std::string sql = "TRUNCATE TABLE " + std::string(TABLE_NAME) + ";";
    pgutils.executeSQL(sql.c_str());
}

// 写入数据到数据库
void KdTreeNodeManager::writeKdTreeNodesToDatabase(int key1, int key2, const std::vector<DBKdtreeNode>& dbNodes) {
    // 使用SPI接口插入双键二进制数据
    pgutils.executeBinaryInsertDualKey(TABLE_NAME, key1, key2, dbNodes.data(), dbNodes.size() * sizeof(DBKdtreeNode));
}

void KdTreeNodeManager::updateKdTreeNodesInDatabase(int key1, int key2, const std::vector<DBKdtreeNode>& updatedNodes) {
    // 使用SPI接口的UPSERT操作（INSERT ... ON CONFLICT ... DO UPDATE）
    pgutils.executeBinaryUpsertDualKey(TABLE_NAME, key1, key2, updatedNodes.data(), updatedNodes.size() * sizeof(DBKdtreeNode));
}


// 从数据库读取数据
std::vector<DBKdtreeNode> KdTreeNodeManager::loadKdTreeNodesFromDatabase(int key1, int key2) {
    std::vector<DBKdtreeNode> dbNodes;

    // 使用SPI接口查询双键二进制数据
    BinarySelectResult* result = pgutils.executeBinarySelectByDualKey(TABLE_NAME, key1, key2);
    
    if (result == NULL) {
        throw std::runtime_error("Kdtree load two key empty");
    }

    size_t node_size = sizeof(DBKdtreeNode);
    size_t total_size = result->size;

    if (total_size % node_size != 0) {
        std::cerr << "Error: The binary data size does not match the expected size for DBKdtreeNode." << std::endl;
        pfree(result->data);
        pfree(result);
        return dbNodes;  // 发生错误，返回空的 dbNodes
    }

    int num_nodes = total_size / node_size;
    const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(result->data);
    dbNodes.assign(buffer, buffer + num_nodes);

    // 释放SPI分配的内存
    pfree(result->data);
    pfree(result);

    return dbNodes;
}

std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> KdTreeNodeManager::loadKdTreeNodesFromDatabase(
    const std::vector<std::pair<int, int>>& keyPairs) {

    std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> keyPairNodeMap;

    if (keyPairs.empty()) {
        throw std::runtime_error("Key pairs list is empty");
    }

    // 使用SPI接口逐个查询每个键对
    for (const auto& keyPair : keyPairs) {
        int key1 = keyPair.first;
        int key2 = keyPair.second;

        BinarySelectResult* result = pgutils.executeBinarySelectByDualKey(TABLE_NAME, key1, key2);
        
        if (result != NULL) {
            size_t node_size = sizeof(DBKdtreeNode);
            size_t total_size = result->size;

            if (total_size % node_size != 0) {
                std::cerr << "Error: The binary data size does not match the expected size for DBKdtreeNode." << std::endl;
                pfree(result->data);
                pfree(result);
                continue;
            }

            int num_nodes = total_size / node_size;
            const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(result->data);

            std::vector<DBKdtreeNode> dbNodes(num_nodes);
            std::memcpy(dbNodes.data(), buffer, total_size);

            keyPairNodeMap[key1][key2] = std::move(dbNodes);

            // 释放SPI分配的内存
            pfree(result->data);
            pfree(result);
        }
    }

    return keyPairNodeMap;
}


std::unordered_map<int, std::vector<DBKdtreeNode>> KdTreeNodeManager::loadKdTreeNodesFromDatabase(int key1) {
    std::unordered_map<int, std::vector<DBKdtreeNode>> key2NodeMap;

    // 使用SPI接口查询指定key1的所有记录
    DualKeyBinarySelectResult* result = pgutils.executeBinarySelectByKey1(TABLE_NAME, key1);
    
    if (result == NULL) {
        throw std::runtime_error("Kdtree load one key empty");
    }

    size_t node_size = sizeof(DBKdtreeNode);
    
    // 处理每个结果记录
    for (int i = 0; i < result->count; i++) {
        int key2 = result->key2_array[i];
        size_t total_size = result->size_array[i];

        if (total_size % node_size != 0) {
            std::cerr << "Error: The binary data size does not match the expected size for DBKdtreeNode." << std::endl;
            continue;  // 跳过当前记录
        }

        int num_nodes = total_size / node_size;
        const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(result->data_array[i]);

        // 将数据解压到 vector 中
        std::vector<DBKdtreeNode> dbNodes(num_nodes);
        std::memcpy(dbNodes.data(), buffer, total_size);

        // 存入 map
        key2NodeMap[key2] = std::move(dbNodes);
    }

    // 释放SPI分配的内存
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

std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> KdTreeNodeManager::loadAllKdTreeNodes() {
    std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> all_kd_nodes;
    long long size_count = 0;

    // 使用SPI接口查询所有双键记录
    DualKeyBinarySelectResult* result = pgutils.executeBinarySelectAllDualKey(TABLE_NAME);
    
    if (result != NULL) {
        size_t node_size = sizeof(DBKdtreeNode);
        
        // 处理每个结果记录
        for (int i = 0; i < result->count; i++) {
            int key1 = result->key1_array[i];
            int key2 = result->key2_array[i];
            size_t total_size = result->size_array[i];
            
            // 解析二进制数据
            int num_nodes = total_size / node_size;
            const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(result->data_array[i]);

            std::vector<DBKdtreeNode> nodes(num_nodes);
            size_count += nodes.size();
            std::memcpy(nodes.data(), buffer, total_size);
            
            // 存储到双层 map
            all_kd_nodes[key1][key2] = std::move(nodes);
        }
        
        // 释放SPI分配的内存
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
    }
    
    std::cout <<"all_kd_nodes size_count:"<<size_count<< std::endl;
    return all_kd_nodes;
}