#include "../include/safe_header.h"
#include "../include/kdtree_node_manager.h"
#include "../include/pgutils.h"

void KdTreeNodeManager::clearTable() {
    // 使用PostgreSQL Extension标准的SPI方式执行SQL
    // 表已在扩展安装时创建，这里只清理数据
    
    // 截断表中的数据
    execute_sql("TRUNCATE TABLE all_kdtree;");
}

// 写入数据到数据库
void KdTreeNodeManager::writeKdTreeNodesToDatabase(int key1, int key2, const std::vector<DBKdtreeNode>& dbNodes) {
    auto connection = PG_pool->acquire();  // 从连接池获取连接
    pqxx::connection &C = *connection;

  
        pqxx::work txn(C);

        // 序列化 dbNodes 为二进制数据
        pqxx::binarystring binary_string(reinterpret_cast<const char*>(dbNodes.data()), dbNodes.size() * sizeof(DBKdtreeNode));

        txn.exec0(
            "INSERT INTO all_kdtree (key1, key2, data) VALUES (" + txn.quote(key1) + ", " + txn.quote(key2) + ", " + txn.quote(binary_string) + ");"
        );

        txn.commit();  // 提交事务

    PG_pool->release(connection);  // 释放连接
}

void KdTreeNodeManager::updateKdTreeNodesInDatabase(int key1, int key2, const std::vector<DBKdtreeNode>& updatedNodes) {
     auto connection = PG_pool->acquire();  // Acquire connection from pool
     pqxx::connection& C = *connection;

     try {
         pqxx::work txn(C);

         // Serialize the updated nodes to binary
         pqxx::binarystring binary_data(reinterpret_cast<const char*>(updatedNodes.data()),
                                     updatedNodes.size() * sizeof(DBKdtreeNode));

         // Check if record exists first
         pqxx::result res = txn.exec_params(
             "SELECT 1 FROM all_kdtree WHERE key1 = $1 AND key2 = $2",
             key1,
             key2
         );

         if (res.empty()) {
             // If no existing record, insert a new one (optional)
             txn.exec_params(
                "INSERT INTO all_kdtree (key1, key2, data) VALUES ($1, $2, $3) "
                "ON CONFLICT (key1, key2) DO UPDATE SET data = $3",
                key1,
                key2,
                binary_data
            );
         } else {
             // Update existing record
             txn.exec_params(
                 "UPDATE all_kdtree SET data = $3 "
                 "WHERE key1 = $1 AND key2 = $2",
                 key1,
                 key2,
                 binary_data
             );
         }

         txn.commit();
     } catch (const std::exception& e) {
         // Rollback happens automatically when work goes out of scope
         PG_pool->release(connection);
         throw;  // Re-throw the exception
     }

     PG_pool->release(connection);
 }


// 从数据库读取数据
std::vector<DBKdtreeNode> KdTreeNodeManager::loadKdTreeNodesFromDatabase(int key1, int key2) {
    std::vector<DBKdtreeNode> dbNodes;

    auto connection = PG_pool->acquire();  // 从连接池获取连接
    pqxx::connection &C = *connection;

    pqxx::nontransaction txn(C);

    // 查询数据库
    pqxx::result db_result = txn.exec(
        "SELECT data FROM all_kdtree WHERE key1 = " + txn.quote(key1) + " AND key2 = " + txn.quote(key2) + ";"
    );

    if (!db_result.empty()) {
        pqxx::binarystring bin(db_result[0][0]);
        size_t node_size = sizeof(DBKdtreeNode);
        size_t total_size = bin.size();

        if (total_size % node_size != 0) {
            std::cerr << "Error: The binary data size does not match the expected size for DBKdtreeNode." << std::endl;
            return dbNodes;  // 发生错误，返回空的 dbNodes
        }

        int num_nodes = total_size / node_size;
        const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(bin.data());
        dbNodes.assign(buffer,buffer+num_nodes);
    } else {
        throw StringException("Kdtree load two key empty");
    }
    txn.commit();
    PG_pool->release(connection);  // 释放连接
    return dbNodes;
}

std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> KdTreeNodeManager::loadKdTreeNodesFromDatabase(
    const std::vector<std::pair<int, int>>& keyPairs) {

    std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> keyPairNodeMap;

    if (keyPairs.empty()) {
        throw StringException("Key pairs list is empty");
    }

    // Define batch size
    const size_t batch_size = 1000;  // Adjust this number based on your system's limits
    size_t total_batches = (keyPairs.size() + batch_size - 1) / batch_size;

    auto connection = PG_pool->acquire();
    pqxx::connection& C = *connection;
    pqxx::nontransaction txn(C);

    for (size_t batch_idx = 0; batch_idx < total_batches; ++batch_idx) {
        // Prepare the current batch
        size_t start_idx = batch_idx * batch_size;
        size_t end_idx = std::min((batch_idx + 1) * batch_size, keyPairs.size());
        std::string inClause;
        for (size_t i = start_idx; i < end_idx; ++i) {
            const auto& keyPair = keyPairs[i];
            inClause += "(" + txn.quote(keyPair.first) + "," + txn.quote(keyPair.second) + "),";
        }
        inClause.pop_back();

        // Execute query for the current batch
        pqxx::result db_result = txn.exec(
            "SELECT key1, key2, data FROM all_kdtree WHERE (key1, key2) IN (" + inClause + ");"
        );

        if (!db_result.empty()) {
            for (const auto& row : db_result) {
                int key1 = row[0].as<int>();
                int key2 = row[1].as<int>();

                pqxx::binarystring bin(row[2]);
                size_t node_size = sizeof(DBKdtreeNode);
                size_t total_size = bin.size();

                if (total_size % node_size != 0) {
                    std::cerr << "Error: The binary data size does not match the expected size for DBKdtreeNode." << std::endl;
                    continue;
                }

                int num_nodes = total_size / node_size;
                const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(bin.data());

                std::vector<DBKdtreeNode> dbNodes(num_nodes);
                std::memcpy(dbNodes.data(), bin.data(), total_size);

                keyPairNodeMap[key1][key2] = std::move(dbNodes);
            }
        } else {
            std::cerr << "Warning: No data found for the given key pairs in batch " << batch_idx + 1 << std::endl;
        }
    }

    txn.commit();
    PG_pool->release(connection);
    return keyPairNodeMap;
}


std::unordered_map<int, std::vector<DBKdtreeNode>> KdTreeNodeManager::loadKdTreeNodesFromDatabase(int key1) {
    std::unordered_map<int, std::vector<DBKdtreeNode>> key2NodeMap;

    auto connection = PG_pool->acquire();  // 从连接池获取连接
    pqxx::connection &C = *connection;

    pqxx::nontransaction txn(C);

    // 查询数据库
    pqxx::result db_result = txn.exec(
        "SELECT key2, data FROM all_kdtree WHERE key1 = " + txn.quote(key1) + ";"
    );
    if (!db_result.empty()) {
        for (const auto &row : db_result) {
            int key2 = row[0].as<int>();
            key2NodeMap[key2] = {};
        }
        auto splited_task = split<std::int64_t>(0,db_result.size(),thread_pool_size);
        for(auto sub_task:splited_task){
            thread_pool.post_task([begin = sub_task.first,end = sub_task.second,&db_result,&key2NodeMap](){
                for(std::int64_t i = begin;i<end;++i){
                    auto const &row = db_result[i];
                    int key2 = row[0].as<int>();
                    // 获取二进制数据
                    pqxx::binarystring bin(row[1]);
                    size_t node_size = sizeof(DBKdtreeNode);
                    size_t total_size = bin.size();

                    if (total_size % node_size != 0) {
                        std::cerr << "Error: The binary data size does not match the expected size for DBKdtreeNode." << std::endl;
                        continue;  // 跳过当前记录
                    }
                    int num_nodes = total_size / node_size;
                    const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(bin.data());
                    key2NodeMap[key2].assign(buffer,buffer+num_nodes);
                }
            });
        }
        thread_pool.wait_for_all_tasks();
        
        // for (const auto &row : db_result) {
        //     // 获取 key2
        //     int key2 = row[0].as<int>();

        //     // 获取二进制数据
        //     pqxx::binarystring bin(row[1]);
        //     size_t node_size = sizeof(DBKdtreeNode);
        //     size_t total_size = bin.size();

        //     if (total_size % node_size != 0) {
        //         std::cerr << "Error: The binary data size does not match the expected size for DBKdtreeNode." << std::endl;
        //         continue;  // 跳过当前记录
        //     }

        //     int num_nodes = total_size / node_size;
        //     const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(bin.data());

        //     // 将数据解压到 vector 中
        //     std::vector<DBKdtreeNode> dbNodes(num_nodes);
        //     std::memcpy(dbNodes.data(), bin.data(), total_size);

        //     // 存入 map
        //     key2NodeMap[key2] = std::move(dbNodes);
        // }
    } else {
        throw StringException("Kdtree load one key empty");
    }
    txn.commit();
    PG_pool->release(connection);  // 释放连接
    return key2NodeMap;
}

#include <unordered_map>
#include <vector>
#include <future>
#include <mutex>

std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> KdTreeNodeManager::loadAllKdTreeNodes() {
    std::unordered_map<int, std::unordered_map<int, std::vector<DBKdtreeNode>>> all_kd_nodes;
    long long size_count = 0;

        auto connection = PG_pool->acquire();  // 从连接池获取连接
        pqxx::connection &C = *connection;

        pqxx::nontransaction txn(C);

        // 查询所有数据
        pqxx::result db_result = txn.exec("SELECT key1, key2, data FROM all_kdtree;");

        for (const auto &row : db_result) {
            int key1 = row[0].as<int>();
            int key2 = row[1].as<int>();
            pqxx::binarystring bin(row[2]);
            size_t node_size = sizeof(DBKdtreeNode);
            size_t total_size = bin.size();
            // 解析二进制数据
            int num_nodes = total_size / node_size;
            const DBKdtreeNode* buffer = reinterpret_cast<const DBKdtreeNode*>(bin.data());

            std::vector<DBKdtreeNode> nodes(num_nodes);
            size_count += nodes.size();
            std::memcpy(nodes.data(), bin.data(), total_size);
            // 存储到双层 map
            all_kd_nodes[key1][key2] = std::move(nodes);
            
        }
    std::cout <<"all_kd_nodes size_count:"<<size_count<< std::endl;
    txn.commit();
        PG_pool->release(connection);  // 释放连接
    return all_kd_nodes;
}