#ifndef USERDATA_MANAGER_H
#define USERDATA_MANAGER_H

#include <vector>
#include <unordered_map>
#include "spatiotemporal_data.h"

/**
 * @brief Manager class for user data operations using singleton pattern
 * 
 * This class provides comprehensive database operations for storing and retrieving
 * user spatiotemporal data in PostgreSQL database using large object storage.
 * It uses singleton pattern to ensure global unique instance and provides 
 * thread-safe operations for user data management.
 * 
 * Key features:
 * - Singleton pattern for global access and consistency
 * - Large object storage for efficient handling of massive datasets
 * - Binary data storage for optimal performance
 * - Batch operations for multiple data chunks
 * - Data validation and integrity checking
 * - Comprehensive error handling and logging
 * 
 * Database schema:
 * - Table: user_data
 * - Primary key: key (single integer key)
 * - Data: Binary serialized SpatioTemporalData arrays as large objects
 * 
 * Data structure:
 * - Each key can map to multiple large objects (data chunks)
 * - Each large object contains an array of SpatioTemporalData structures
 * - Supports variable-length data arrays per key
 * 
 * Usage patterns:
 * - key: typically represents user ID, session ID, or data partition
 * - data: vector of SpatioTemporalData representing user's spatial data
 * - Multiple chunks per key for handling large datasets efficiently
 */
class UserDataManager {
public:
    static constexpr const char* TABLE_NAME = "user_data";
    
    /**
     * @brief Get singleton instance of UserDataManager
     * 
     * Implements thread-safe lazy initialization using static local variable.
     * The first call creates the instance, subsequent calls return the same instance.
     * 
     * @return UserDataManager& Reference to the singleton instance
     * 
     * @note Thread-safe in C++11 and later due to static local variable initialization
     * @note Memory is automatically managed by the runtime
     */
    static UserDataManager& getInstance();
    
    // Delete copy constructor and assignment operations to enforce singleton
    UserDataManager(const UserDataManager&) = delete;
    UserDataManager& operator=(const UserDataManager&) = delete;
    UserDataManager(UserDataManager&&) = delete;
    UserDataManager& operator=(UserDataManager&&) = delete;
    
    /**
     * @brief Clear all data from the user data table
     * 
     * Truncates the entire user data table to remove all existing data.
     * This operation is typically performed before loading new user data
     * to ensure a clean state and avoid data conflicts.
     * 
     * Database operation:
     * - Executes TRUNCATE TABLE command for complete data removal
     * - More efficient than DELETE for removing all rows
     * - Automatically removes associated large objects
     * - Resets any auto-increment counters
     * - Cannot be rolled back in most database configurations
     * 
     * @note This operation is irreversible - all user data will be lost
     * @note Logs the operation for debugging and monitoring purposes
     * @note Handles large object cleanup automatically
     */
    void clearTable();

    /**
     * @brief Write user data to database using large object storage
     * 
     * Stores user spatiotemporal data in the database using PostgreSQL's
     * large object storage mechanism. This approach is optimized for
     * handling massive datasets that exceed regular column size limits.
     * 
     * Storage process:
     * 1. Validate input data for consistency and size
     * 2. Create large object for binary data storage
     * 3. Serialize SpatioTemporalData array to binary format
     * 4. Store binary data in PostgreSQL large object
     * 5. Associate large object with the specified key
     * 
     * Large object benefits:
     * - Efficient storage of large binary data (>1GB supported)
     * - Streaming access for massive datasets
     * - Automatic garbage collection when table is truncated
     * - Optimal performance for bulk data operations
     * 
     * @param key Unique identifier for the user data
     * @param data Vector of SpatioTemporalData to store
     * 
     * @note Uses PostgreSQL large object interface for massive data support
     * @note Automatically handles binary serialization and storage
     * @note Multiple data chunks can be associated with same key
     * @note Provides automatic data validation and integrity checking
     * 
     * @throws std::runtime_error if data validation fails
     * @throws std::runtime_error if large object creation fails
     * @throws std::runtime_error if data serialization fails
     */
    void writeDataToDatabase(int key, const std::vector<SpatioTemporalData>& data);

    /**
     * @brief Load user data from database by key
     * 
     * Retrieves and deserializes user spatiotemporal data associated with
     * the specified key. Handles multiple data chunks per key and provides
     * comprehensive data validation and integrity checking.
     * 
     * Retrieval process:
     * 1. Query database for all large objects associated with the key
     * 2. Validate each large object for data integrity
     * 3. Deserialize binary data back to SpatioTemporalData structures
     * 4. Organize data into separate vectors per large object
     * 5. Return vector of data vectors for batch processing
     * 
     * Data organization:
     * - Each key can have multiple associated large objects
     * - Each large object becomes a separate vector in the result
     * - Maintains chronological order of data insertion
     * - Supports efficient batch processing of large datasets
     * 
     * @param key Unique identifier for the user data to retrieve
     * @return std::vector<std::vector<SpatioTemporalData>> Vector of data vectors
     * 
     * @note Returns empty vector if no data found for the key
     * @note Each inner vector represents data from one large object
     * @note Automatically handles memory management for large objects
     * @note Validates data integrity before returning results
     * 
     * @throws std::runtime_error if data corruption detected
     * @throws std::runtime_error if deserialization fails
     * @throws std::runtime_error if memory allocation fails
     */
    std::vector<std::vector<SpatioTemporalData>> loadDataFromDatabase(int key);

private:
    /**
     * @brief Private constructor for singleton pattern
     * 
     * Prevents direct instantiation of the class, enforcing singleton behavior.
     * Initialization is performed automatically when getInstance() is first called.
     */
    UserDataManager() = default;
    
    /**
     * @brief Private destructor for singleton pattern
     * 
     * Handles cleanup of any resources when the application terminates.
     * Called automatically by the runtime for static instance cleanup.
     */
    ~UserDataManager() = default;
};

/**
 * @brief Global singleton instance of UserDataManager
 * 
 * This provides convenient global access to the user data manager instance
 * throughout the application, similar to other manager classes.
 * 
 * Usage:
 * ```cpp
 * userDataManager.writeDataToDatabase(key, data);
 * auto user_data = userDataManager.loadDataFromDatabase(key);
 * ```
 */
extern UserDataManager& userDataManager;

#endif // USERDATA_MANAGER_H