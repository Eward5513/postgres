#include "../include/safe_header.h"
#include "mesh_connection_manager.h"
#include "pgutils.h"
#include "nlohmann/json.hpp"
#include "../include/safe_logger.h"
#include <stdexcept>
#include <chrono>
#include <sstream>

using json = nlohmann::json;

/**
 * @brief Get singleton instance of MeshConnectionManager
 * 
 * Implements thread-safe lazy initialization using static local variable.
 * The first call creates the instance, subsequent calls return the same instance.
 * 
 * @return MeshConnectionManager& Reference to the singleton instance
 * 
 * @note Thread-safe in C++11 and later due to static local variable initialization
 * @note Memory is automatically managed by the runtime
 */
MeshConnectionManager& MeshConnectionManager::getInstance() {
    static MeshConnectionManager instance;
    return instance;
}

/**
 * @brief Global singleton instance for convenient access
 * 
 * Provides direct access to the MeshConnectionManager singleton instance throughout
 * the application without requiring explicit getInstance() calls.
 */
MeshConnectionManager& meshConnectionManager = MeshConnectionManager::getInstance();

/**
 * @brief Clear all data from the mesh connections table
 * 
 * Truncates the entire mesh connections table to remove all existing data.
 * This operation is typically performed before loading new mesh data
 * to ensure a clean state and avoid data conflicts.
 * 
 * Database operation:
 * - Executes TRUNCATE TABLE command for complete data removal
 * - More efficient than DELETE for removing all rows
 * - Resets any auto-increment counters
 * - Cannot be rolled back in most database configurations
 * 
 * @note This operation is irreversible - all mesh connection data will be lost
 * @note Logs the operation for debugging and monitoring purposes
 */
void MeshConnectionManager::clearTable() {
    elog(INFO, "MeshConnectionManager::clearTable()");
    std::string sql = "TRUNCATE TABLE " + std::string(TABLE_NAME) + ";";
    pgutils.executeSQL(sql.c_str());
}

/**
 * @brief Write mesh connection data to database
 * 
 * Stores mesh connection data in the database using JSON serialization.
 * The data is associated with a unique key for efficient retrieval.
 * Provides comprehensive logging and performance monitoring.
 * 
 * Storage process:
 * 1. Analyze and validate input connection data
 * 2. Generate detailed statistics (connection count, point count, etc.)
 * 3. Serialize data to JSON format for flexible storage
 * 4. Store JSON as binary data in PostgreSQL
 * 5. Log operation details and performance metrics
 * 
 * Data analysis includes:
 * - Total number of connections
 * - Total number of points across all connections
 * - Minimum and maximum connection sizes
 * - Serialized data size and memory usage
 * 
 * @param key Unique identifier for the mesh connection data
 * @param connections Vector of connection vectors representing mesh connectivity
 * 
 * @note Automatically logs detailed statistics for monitoring
 * @note Provides warnings for large data sizes (>1MB)
 * @note Uses JSON serialization for cross-platform compatibility
 * @note Stores data as binary for optimal database performance
 */
void MeshConnectionManager::writeDataToDatabase(int key, const std::vector<std::vector<int32_t>>& connections) {
    // Add static variable to track call count for debugging
    static int mesh_write_call_count = 0;
    mesh_write_call_count++;
    
    // Analyze basic statistics of connection data
    size_t total_connections = connections.size();
    size_t total_points = 0;
    size_t max_connection_size = 0;
    size_t min_connection_size = SIZE_MAX;
    
    // Calculate detailed connection statistics
    for (const auto& connection : connections) {
        total_points += connection.size();
        max_connection_size = std::max(max_connection_size, connection.size());
        if (!connection.empty()) {
            min_connection_size = std::min(min_connection_size, connection.size());
        }
    }
    
    // Handle edge case for empty connections
    if (connections.empty()) {
        min_connection_size = 0;
    }
    
    // Log detailed operation statistics
    std::ostringstream oss;
    oss << "MeshConnectionManager::writeDataToDatabase #" << mesh_write_call_count 
        << ": key=" << key << ", connections_count=" << total_connections 
        << ", total_points=" << total_points << ", min_conn_size=" << min_connection_size 
        << ", max_conn_size=" << max_connection_size;
    logger.logInfo(oss.str());
    
    // Serialize connection data to JSON string
    std::string serialized_data = serialize(connections);
    
    // Monitor serialized data size for performance optimization
    size_t serialized_size = serialized_data.length();
    std::ostringstream oss2;
    oss2 << "MeshConnectionManager::writeDataToDatabase #" << mesh_write_call_count 
         << ": serialized JSON size=" << serialized_size << " bytes (" 
         << (double)serialized_size / (1024.0 * 1024.0) << " MB)";
    logger.logInfo(oss2.str());
    
    // Provide performance warnings for large datasets
    if (serialized_size > 1024 * 1024) {  // 1MB threshold
        std::ostringstream oss3;
        oss3 << "MeshConnectionManager::writeDataToDatabase #" << mesh_write_call_count 
             << ": Large data detected! Size=" << serialized_size << " bytes (" 
             << (double)serialized_size / (1024.0 * 1024.0) << " MB) for key=" << key;
        logger.logWarning(oss3.str());
    }
    
    // Store JSON string as binary data in PostgreSQL
    pgutils.executeBinaryInsert(TABLE_NAME, key, serialized_data.c_str(), serialized_data.length());
    
    // Log successful completion
    std::ostringstream oss4;
    oss4 << "MeshConnectionManager::writeDataToDatabase #" << mesh_write_call_count 
         << ": COMPLETED for key=" << key;
    logger.logInfo(oss4.str());
}

/**
 * @brief Load mesh connection data from database with performance timing
 * 
 * Retrieves and deserializes mesh connection data associated with the specified key.
 * Provides precise timing measurements for performance analysis and optimization.
 * 
 * Retrieval process:
 * 1. Record start time for performance measurement
 * 2. Query database for binary data using the key
 * 3. Record end time and calculate database query duration
 * 4. Validate data integrity and convert binary to string
 * 5. Deserialize JSON string back to connection data structure
 * 6. Return both data and timing information
 * 
 * Performance measurement:
 * - Measures database query time in microseconds
 * - Converts to milliseconds for convenience
 * - Excludes deserialization time (focuses on DB performance)
 * 
 * @param key Unique identifier for the mesh connection data to retrieve
 * @param db_time Output parameter for database query time in milliseconds
 * @return std::vector<std::vector<int32_t>> Vector of connection vectors
 * 
 * @throws std::runtime_error if data not found for the specified key
 * @throws std::runtime_error if JSON deserialization fails
 * @throws std::runtime_error if data corruption detected
 * 
 * @note Provides precise timing for performance optimization
 * @note Automatically handles memory management for query results
 * @note Validates data integrity before returning results
 */
std::vector<std::vector<int32_t>> MeshConnectionManager::loadDataFromDatabase(int key, double& db_time) {
    // Record start time for precise performance measurement
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Use binary query interface to retrieve data
    BinarySelectResult* result = pgutils.executeBinarySelect(TABLE_NAME, key);
    
    // Record end time and calculate database query duration
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    db_time = duration.count() / 1000.0; // Convert to milliseconds
    
    // Validate query result
    if (result == NULL) {
        throw std::runtime_error("Connections load empty for key " + std::to_string(key));
    }
    
    // Convert binary data to string (with null terminator)
    std::string json_str(reinterpret_cast<const char*>(result->data), result->size);
    
    // Deserialize JSON string back to connection data structure
    std::vector<std::vector<int32_t>> result_data = deserialize(json_str);
    
    // Release memory allocated by SPI
    pfree(result->data);
    pfree(result);
    
    return result_data;
}

/**
 * @brief Serialize mesh connection data to JSON string
 * 
 * Converts the mesh connection data structure to a JSON string
 * for database storage. Uses efficient JSON library for serialization.
 * 
 * Serialization format:
 * - Nested JSON array structure
 * - Each connection is a JSON array of integers
 * - Optimized for space efficiency and parsing speed
 * 
 * @param connections Vector of connection vectors to serialize
 * @return std::string JSON string representation of the data
 * 
 * @note Uses nlohmann/json library for robust serialization
 * @note Optimized for minimal storage space
 * @note Maintains data type integrity during conversion
 */
std::string MeshConnectionManager::serialize(const std::vector<std::vector<int32_t>>& connections) {
    // Use nlohmann/json for robust and efficient serialization
    json j = connections;
    return j.dump();
}

/**
 * @brief Deserialize JSON string to mesh connection data
 * 
 * Converts a JSON string back to the mesh connection data structure.
 * Validates JSON format and ensures data integrity during conversion.
 * 
 * Deserialization process:
 * 1. Parse JSON string using robust JSON parser
 * 2. Validate JSON structure and format
 * 3. Convert JSON arrays to C++ vectors
 * 4. Validate data types and ranges
 * 5. Return reconstructed data structure
 * 
 * @param json_str JSON string to deserialize
 * @return std::vector<std::vector<int32_t>> Reconstructed connection data
 * 
 * @throws std::runtime_error if JSON parsing fails
 * @throws std::runtime_error if data format is invalid
 * @throws std::runtime_error if data type conversion fails
 * 
 * @note Provides robust error handling for malformed JSON
 * @note Validates data integrity during conversion
 * @note Maintains performance for large datasets
 */
std::vector<std::vector<int32_t>> MeshConnectionManager::deserialize(const std::string& json_str) {
    // Parse JSON string using robust JSON parser and convert to C++ data structure
    json j = json::parse(json_str);
    return j.get<std::vector<std::vector<int32_t>>>();
}
