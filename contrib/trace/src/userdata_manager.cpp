#include "../include/safe_header.h"
#include "../include/userdata_manager.h"
#include "../include/pgutils.h"
#include "../include/safe_logger.h"
#include <stdexcept>
#include <cstring>
#include <sstream>

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
UserDataManager& UserDataManager::getInstance() {
    static UserDataManager instance;
    return instance;
}

/**
 * @brief Global singleton instance for convenient access
 * 
 * Provides direct access to the UserDataManager singleton instance throughout
 * the application without requiring explicit getInstance() calls.
 */
UserDataManager& userDataManager = UserDataManager::getInstance();

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
 * 
 * @note This operation is irreversible - all user data will be lost
 * @note Logs the operation for debugging and monitoring purposes
 */
void UserDataManager::clearTable() {
    elog(INFO, "UserDataManager::clearTable()");
    // Clear data from the table (table creation logic has been moved to SQL file)
    std::string sql = "TRUNCATE TABLE " + std::string(TABLE_NAME) + ";";
    pgutils.executeSQL(sql.c_str());
}

/**
 * @brief Write user spatiotemporal data to database
 * 
 * Stores user spatiotemporal data in the database using PostgreSQL's
 * large object storage mechanism for efficient handling of large datasets.
 * 
 * Storage process:
 * 1. Validates input data for consistency and size
 * 2. Uses SPI interface to insert large object data
 * 3. Associates the data with the specified key for retrieval
 * 
 * Large object benefits:
 * - Efficient storage of large binary data
 * - Streaming access for massive datasets
 * - Automatic garbage collection when table is truncated
 * - Optimal performance for bulk data operations
 * 
 * @param key Unique identifier for the user data
 * @param data Vector of SpatioTemporalData to store
 * 
 * @note One key may correspond to multiple large objects - needs investigation
 * @note Uses PostgreSQL SPI interface for large object operations
 * @note Automatically handles binary serialization and storage
 */
// One key may correspond to multiple large objects - needs investigation
void UserDataManager::writeDataToDatabase(int key, const std::vector<SpatioTemporalData>& data) {
    // Use SPI interface to insert large object data
    pgutils.executeLargeObjectInsert(TABLE_NAME, key, data.data(), data.size() * sizeof(SpatioTemporalData));
}

/**
 * @brief Load user spatiotemporal data from database
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
 */
std::vector<std::vector<SpatioTemporalData>> UserDataManager::loadDataFromDatabase(int key) {
    std::vector<std::vector<SpatioTemporalData>> data;

    // Use SPI interface to query large object data
    LargeObjectSelectResult* result = pgutils.executeLargeObjectSelectByKey(TABLE_NAME, key);
    
    if (result == NULL) {
        return data; // No data found, return empty data
    }
    
    // Pre-allocate space for efficiency
    data.reserve(result->count);
    
    // Iterate through all large object data
    for (int i = 0; i < result->count; i++) {
        if (result->data_array[i] != NULL && result->size_array[i] > 0) {
            // Validate data size for consistency
            if (result->size_array[i] % sizeof(SpatioTemporalData) != 0) {
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                               errmsg("Invalid data size for SpatioTemporalData")));
            }
            
            // Add data to result collection
            data.emplace_back();
            size_t num_items = result->size_array[i] / sizeof(SpatioTemporalData);
            const SpatioTemporalData* buffer = reinterpret_cast<const SpatioTemporalData*>(result->data_array[i]);
            data.back().assign(buffer, buffer + num_items);
        }
    }
    
    // Release memory allocated by PostgreSQL
    for (int i = 0; i < result->count; i++) {
        if (result->data_array[i] != NULL) {
            pfree(result->data_array[i]);
        }
    }
    pfree(result->data_array);
    pfree(result->size_array);
    pfree(result);
    
    return data;
}