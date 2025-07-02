#ifndef MESH_CONNECTION_MANAGER_H
#define MESH_CONNECTION_MANAGER_H

#include <vector>
#include <string>
#include <cstdint>

/**
 * 网格连接管理器类
 * 负责管理网格连接数据的存储和检索
 */
class MeshConnectionManager {
public:
    /**
     * 清空mesh连接表
     */
    static void clearTable();

    /**
     * 将连接数据写入数据库
     * @param key 数据键值
     * @param connections 连接数据
     */
    static void writeDataToDatabase(int key, const std::vector<std::vector<int32_t>>& connections);

    /**
     * 从数据库加载连接数据
     * @param key 数据键值
     * @param db_time 数据库查询时间（输出参数）
     * @return 连接数据
     */
    static std::vector<std::vector<int32_t>> loadDataFromDatabase(int key, double& db_time);

private:
    static constexpr const char* TABLE_NAME = "mesh_connections_table";

    /**
     * 序列化连接数据为JSON字符串
     * @param connections 连接数据
     * @return JSON字符串
     */
    static std::string serialize(const std::vector<std::vector<int32_t>>& connections);

    /**
     * 反序列化JSON字符串为连接数据
     * @param json_str JSON字符串
     * @return 连接数据
     */
    static std::vector<std::vector<int32_t>> deserialize(const std::string& json_str);
};

#endif // MESH_CONNECTION_MANAGER_H