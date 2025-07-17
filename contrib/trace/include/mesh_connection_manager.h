#ifndef MESH_CONNECTION_MANAGER_H
#define MESH_CONNECTION_MANAGER_H

#include <vector>
#include <string>
#include <cstdint>

/**
 * @brief Manager class for mesh connection data operations using singleton pattern
 */
class MeshConnectionManager {
public:
    static constexpr const char* TABLE_NAME = "mesh_connections_table";
    
    /**
     * @brief Get singleton instance of MeshConnectionManager
     */
    static MeshConnectionManager& getInstance();
    
    // Delete copy constructor and assignment operations to enforce singleton
    MeshConnectionManager(const MeshConnectionManager&) = delete;
    MeshConnectionManager& operator=(const MeshConnectionManager&) = delete;
    MeshConnectionManager(MeshConnectionManager&&) = delete;
    MeshConnectionManager& operator=(MeshConnectionManager&&) = delete;
    
    /**
     * @brief Clear all data from the mesh connections table
     */
    void clearTable();

    /**
     * @brief Write mesh connection data to database
     */
    void writeDataToDatabase(int key, const std::vector<std::vector<int32_t>>& connections);

    /**
     * @brief Load mesh connection data from database with performance timing
     */
    std::vector<std::vector<int32_t>> loadDataFromDatabase(int key, double& db_time);

private:
    /**
     * @brief Private constructor for singleton pattern
     */
    MeshConnectionManager() = default;

    /**
     * @brief Private destructor for singleton pattern
     */
    ~MeshConnectionManager() = default;

    /**
     * @brief Serialize mesh connection data to JSON string
     */
    std::string serialize(const std::vector<std::vector<int32_t>>& connections);

    /**
     * @brief Deserialize JSON string to mesh connection data
     */
    std::vector<std::vector<int32_t>> deserialize(const std::string& json_str);
};

/**
 * @brief Global singleton instance of MeshConnectionManager
 */
extern MeshConnectionManager& meshConnectionManager;

#endif // MESH_CONNECTION_MANAGER_H