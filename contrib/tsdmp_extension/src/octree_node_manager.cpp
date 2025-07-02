#include "../include/safe_header.h"
#include "../include/octree_node_manager.h"
#include "../include/pgutils.h"

void OctreeNodeManager::clearTable() {
    // 使用PostgreSQL Extension标准的SPI方式执行SQL
    // 表已在扩展安装时创建，这里只清理数据
    
    // 截断表中的数据
    execute_sql("TRUNCATE TABLE all_octree_table;");
}

// 写入数据到数据库
void OctreeNodeManager::writeOctreeNodesToDatabase(int key, const std::vector<DBOctreeNode>& dbNodes) {
    auto connection = PG_pool->acquire();  // 从连接池获取连接
    pqxx::connection &C = *connection;

        pqxx::work txn(C);

        pqxx::binarystring binary_string(reinterpret_cast<const char*>(dbNodes.data()), dbNodes.size() * sizeof(DBOctreeNode));
        txn.exec0(
            "INSERT INTO all_octree_table (key, data) VALUES (" + txn.quote(key) + ", " + txn.quote(binary_string) + ");"
        );

        txn.commit();  // 提交事务

    PG_pool->release(connection);  // 释放连接
}

// 从数据库读取数据
std::vector<DBOctreeNode> OctreeNodeManager::loadOctreeNodesFromDatabase(int key) {
    std::vector<DBOctreeNode> dbNodes;

    auto connection = PG_pool->acquire();  // 从连接池获取连接
    pqxx::connection &C = *connection;

        pqxx::nontransaction txn(C);

        // 查询数据库
        pqxx::result db_result = txn.exec("SELECT data FROM all_octree_table WHERE key = " + txn.quote(key) + ";");

        if (!db_result.empty()) {
            pqxx::binarystring bin(db_result[0][0]);
            int num_nodes = bin.size() / sizeof(DBOctreeNode);
            const DBOctreeNode* buffer = reinterpret_cast<const DBOctreeNode*>(bin.data());

            size_t node_size = sizeof(DBOctreeNode);
            size_t total_size = bin.size();

            if (total_size % node_size != 0) {
                std::cerr << "Error: The binary data size does not match the expected size for DBOctreeNode." << std::endl;
                return dbNodes;  // 发生错误，返回空的 dbNodes
            }

            dbNodes.resize(num_nodes);
            std::memcpy(dbNodes.data(), bin.data(), total_size);
        } else {
            throw StringException("Octree load key empty");
        }
        txn.commit();
    PG_pool->release(connection);  // 释放连接
    return dbNodes;
}


std::unordered_map<int, std::vector<DBOctreeNode>> OctreeNodeManager::loadAllOctreeNodesFromDatabase() {
    std::unordered_map<int, std::vector<DBOctreeNode>> nodeMap;

    auto connection = PG_pool->acquire();  // 从连接池获取连接
    pqxx::connection &C = *connection;

    pqxx::nontransaction txn(C);

    // 查询所有数据
    pqxx::result db_result = txn.exec("SELECT key, data FROM all_octree_table;");
    
    txn.commit();
    PG_pool->release(connection);  // 释放连接
    for (const auto &row : db_result) {
        int key = row[0].as<int>();
        if(nodeMap.find(key) == nodeMap.end()){
            nodeMap[key] = {};
        }
    }
    auto sub_tasks = split<std::int64_t>(0,db_result.size(),thread_pool_size);
    auto do_task = [&](std::int64_t begin,std::int64_t end){
        for(std::int64_t i = begin;i<end;++i){
            auto const &row = db_result[i];
            int key = row[0].as<int>();

            // 解析 binary 数据
            pqxx::binarystring bin(row[1]);
            size_t total_size = bin.size();
            size_t node_size = sizeof(DBOctreeNode);

            if (total_size % node_size != 0) {
                std::cerr << "Error: The binary data size does not match the expected size for DBOctreeNode. Key: " << key << std::endl;
                continue;  // 跳过有问题的行
            }
            int num_nodes = total_size / node_size;
            const DBOctreeNode* buffer = reinterpret_cast<const DBOctreeNode*>(bin.data());
            nodeMap[key].assign(buffer, buffer + num_nodes);
        }
    };
    for(auto task:sub_tasks){
        thread_pool.post_task(
            [&,task](){
                do_task(task.first,task.second);
            }
        );
    }
    thread_pool.wait_for_all_tasks();
    // if (!db_result.empty()) {
    //     for (const auto &row : db_result) {
    //         // 解析 key
    //         int key = row[0].as<int>();

    //         // 解析 binary 数据
    //         pqxx::binarystring bin(row[1]);
    //         size_t total_size = bin.size();
    //         size_t node_size = sizeof(DBOctreeNode);

    //         if (total_size % node_size != 0) {
    //             std::cerr << "Error: The binary data size does not match the expected size for DBOctreeNode. Key: " << key << std::endl;
    //             continue;  // 跳过有问题的行
    //         }

    //         int num_nodes = total_size / node_size;
    //         const DBOctreeNode* buffer = reinterpret_cast<const DBOctreeNode*>(bin.data());
    //         nodeMap[key].assign(buffer, buffer + num_nodes);
    //     }
    // } else {
    //     std::cerr << "No data found in the table." << std::endl;
    // }

    return nodeMap;
}