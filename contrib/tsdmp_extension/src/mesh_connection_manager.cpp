#include "../include/safe_header.h"
#include "mesh_connection_manager.h"
#include "pgutils.h"
#include "nlohmann/json.hpp"
#include <stdexcept>
#include <chrono>

using json = nlohmann::json;

void MeshConnectionManager::clearTable() {
    execute_sql("TRUNCATE TABLE IF EXISTS " + std::string(TABLE_NAME) + ";");
}

void MeshConnectionManager::writeDataToDatabase(int key, const std::vector<std::vector<int32_t>>& connections) {
    // 序列化连接数据
    std::string serialized_data = serialize(connections);
    
    // 构建SQL插入语句
    std::string sql = "INSERT INTO " + std::string(TABLE_NAME) + " (key, data) VALUES (" +
                     std::to_string(key) + ", '" + serialized_data + "');";
    
    execute_sql(sql);
}

std::vector<std::vector<int32_t>> MeshConnectionManager::loadDataFromDatabase(int key, double& db_time) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 构建查询SQL
    std::string sql = "SELECT data FROM " + std::string(TABLE_NAME) + " WHERE key = " + std::to_string(key) + ";";
    
    // 执行查询
    std::vector<std::vector<std::string>> results = execute_sql_select(sql);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    db_time = duration.count() / 1000.0; // 转换为毫秒
    
    if (results.empty()) {
        throw std::runtime_error("Connections load empty for key " + std::to_string(key));
    }
    
    // 反序列化数据
    return deserialize(results[0][0]);
}

std::string MeshConnectionManager::serialize(const std::vector<std::vector<int32_t>>& connections) {
    json j = connections;
    return j.dump();
}

std::vector<std::vector<int32_t>> MeshConnectionManager::deserialize(const std::string& json_str) {
    json j = json::parse(json_str);
    return j.get<std::vector<std::vector<int32_t>>>();
}
