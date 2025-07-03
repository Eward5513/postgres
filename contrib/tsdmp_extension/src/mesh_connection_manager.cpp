#include "../include/safe_header.h"
#include "mesh_connection_manager.h"
#include "pgutils.h"
#include "nlohmann/json.hpp"
#include <stdexcept>
#include <chrono>

using json = nlohmann::json;

void MeshConnectionManager::clearTable() {
    std::string sql = "TRUNCATE TABLE " + std::string(TABLE_NAME) + ";";
    execute_sql(sql.c_str());
}

void MeshConnectionManager::writeDataToDatabase(int key, const std::vector<std::vector<int32_t>>& connections) {
    // 序列化连接数据为JSON字符串
    std::string serialized_data = serialize(connections);
    
    // 将JSON字符串作为二进制数据存储
    execute_binary_insert(TABLE_NAME, key, serialized_data.c_str(), serialized_data.length());
}

std::vector<std::vector<int32_t>> MeshConnectionManager::loadDataFromDatabase(int key, double& db_time) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 使用二进制查询接口
    BinarySelectResult* result = execute_binary_select(TABLE_NAME, key);
    
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
