#include "../include/safe_header.h"
#include "../include/octree_node_manager.h"
#include "../include/pgutils.h"
#include <cstring>

extern "C" {
#include "postgres.h"
#include "utils/elog.h"
}

void OctreeNodeManager::clearTable() {
    elog(INFO, "OctreeNodeManager::clearTable()");

    // 清理表中的数据
    std::string sql = "TRUNCATE TABLE " + std::string(TABLE_NAME) + ";";
    execute_sql(sql.c_str());
}

// 写入数据到数据库
void OctreeNodeManager::writeOctreeNodesToDatabase(int key, const std::vector<DBOctreeNode>& dbNodes) {
    // 使用SPI接口写入二进制数据
    execute_binary_insert(TABLE_NAME, key, dbNodes.data(), dbNodes.size() * sizeof(DBOctreeNode));
}

// 从数据库读取数据
std::vector<DBOctreeNode> OctreeNodeManager::loadOctreeNodesFromDatabase(int key) {
    std::vector<DBOctreeNode> dbNodes;
    
    // 使用SPI接口查询二进制数据
    BinarySelectResult* result = execute_binary_select(TABLE_NAME, key);
    if (result != NULL) {
        // 验证数据大小
        size_t node_size = sizeof(DBOctreeNode);
        if (result->size % node_size != 0) {
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                           errmsg("Binary data size does not match expected size for DBOctreeNode")));
        }
        
        // 转换数据
        int num_nodes = result->size / node_size;
        const DBOctreeNode* buffer = reinterpret_cast<const DBOctreeNode*>(result->data);
        dbNodes.assign(buffer, buffer + num_nodes);
        
        // 释放临时内存
        pfree(result->data);
        pfree(result);
    } else {
        ereport(ERROR, (errcode(ERRCODE_NO_DATA_FOUND),
                       errmsg("Octree data not found for key: %d", key)));
    }
    
    return dbNodes;
}

std::unordered_map<int, std::vector<DBOctreeNode>> OctreeNodeManager::loadAllOctreeNodesFromDatabase() {
    std::unordered_map<int, std::vector<DBOctreeNode>> nodeMap;
    
    // 使用SPI接口查询所有二进制数据
    BinarySelectAllResult* result = execute_binary_select_all(TABLE_NAME);
    if (result != NULL) {
        for (int i = 0; i < result->count; i++) {
            int key = result->keys[i];
            
            if (result->data_array[i] != NULL && result->size_array[i] > 0) {
                // 验证数据大小
                size_t node_size = sizeof(DBOctreeNode);
                if (result->size_array[i] % node_size != 0) {
                    ereport(WARNING, (errcode(ERRCODE_DATA_CORRUPTED),
                                     errmsg("Binary data size does not match expected size for DBOctreeNode. Key: %d", key)));
                    continue;  // 跳过有问题的行
                }
                
                // 转换数据
                int num_nodes = result->size_array[i] / node_size;
                const DBOctreeNode* buffer = reinterpret_cast<const DBOctreeNode*>(result->data_array[i]);
                nodeMap[key].assign(buffer, buffer + num_nodes);
            } else {
                // 创建空的vector
                nodeMap[key] = std::vector<DBOctreeNode>();
            }
        }
        
        // 释放内存
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