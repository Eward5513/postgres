#include "../include/safe_header.h"
#include "mesh_connection_manager.h"
#include "pgutils.h"
#include "nlohmann/json.hpp"
#include "../include/safe_logger.h"
#include <stdexcept>
#include <chrono>
#include <sstream>

using json = nlohmann::json;

void MeshConnectionManager::clearTable() {
    std::string sql = "TRUNCATE TABLE " + std::string(TABLE_NAME) + ";";
    pgutils.executeSQL(sql.c_str());
}

void MeshConnectionManager::writeDataToDatabase(int key, const std::vector<std::vector<int32_t>>& connections) {
    // 添加静态变量跟踪调用次数
    static int mesh_write_call_count = 0;
    mesh_write_call_count++;
    
    // 统计连接数据的基本信息
    size_t total_connections = connections.size();
    size_t total_points = 0;
    size_t max_connection_size = 0;
    size_t min_connection_size = SIZE_MAX;
    
    for (const auto& connection : connections) {
        total_points += connection.size();
        max_connection_size = std::max(max_connection_size, connection.size());
        if (!connection.empty()) {
            min_connection_size = std::min(min_connection_size, connection.size());
        }
    }
    
    if (connections.empty()) {
        min_connection_size = 0;
    }
    
    std::ostringstream oss;
    oss << "MeshConnectionManager::writeDataToDatabase #" << mesh_write_call_count 
        << ": key=" << key << ", connections_count=" << total_connections 
        << ", total_points=" << total_points << ", min_conn_size=" << min_connection_size 
        << ", max_conn_size=" << max_connection_size;
    logger.logInfo(oss.str());
    
    // 序列化连接数据为JSON字符串
    std::string serialized_data = serialize(connections);
    
    // 记录序列化后的数据大小
    size_t serialized_size = serialized_data.length();
    std::ostringstream oss2;
    oss2 << "MeshConnectionManager::writeDataToDatabase #" << mesh_write_call_count 
         << ": serialized JSON size=" << serialized_size << " bytes (" 
         << (double)serialized_size / (1024.0 * 1024.0) << " MB)";
    logger.logInfo(oss2.str());
    
    // 如果数据太大，给出警告
    if (serialized_size > 1024 * 1024) {  // 1MB
        std::ostringstream oss3;
        oss3 << "MeshConnectionManager::writeDataToDatabase #" << mesh_write_call_count 
             << ": Large data detected! Size=" << serialized_size << " bytes (" 
             << (double)serialized_size / (1024.0 * 1024.0) << " MB) for key=" << key;
        logger.logWarning(oss3.str());
    }
    
    // 将JSON字符串作为二进制数据存储
    pgutils.executeBinaryInsert(TABLE_NAME, key, serialized_data.c_str(), serialized_data.length());
    
    std::ostringstream oss4;
    oss4 << "MeshConnectionManager::writeDataToDatabase #" << mesh_write_call_count 
         << ": COMPLETED for key=" << key;
    logger.logInfo(oss4.str());
}

std::vector<std::vector<int32_t>> MeshConnectionManager::loadDataFromDatabase(int key, double& db_time) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 使用二进制查询接口
    BinarySelectResult* result = pgutils.executeBinarySelect(TABLE_NAME, key);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    db_time = duration.count() / 1000.0; // 转换为毫秒
    
    // 检查查询结果
    if (result == NULL) {
        throw std::runtime_error("Connections load empty for key " + std::to_string(key));
    }
    
    // 将二进制数据转换为字符串（添加null终止符）
    std::string json_str(reinterpret_cast<const char*>(result->data), result->size);
    
    // 反序列化数据
    std::vector<std::vector<int32_t>> result_data = deserialize(json_str);
    
    // 释放SPI分配的内存
    pfree(result->data);
    pfree(result);
    
    return result_data;
}

std::string MeshConnectionManager::serialize(const std::vector<std::vector<int32_t>>& connections) {
    json j = connections;
    return j.dump();
}

std::vector<std::vector<int32_t>> MeshConnectionManager::deserialize(const std::string& json_str) {
    json j = json::parse(json_str);
    return j.get<std::vector<std::vector<int32_t>>>();
}
