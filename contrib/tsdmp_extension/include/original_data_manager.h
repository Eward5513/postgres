#ifndef ORIGINAL_DATA_MANAGER_H
#define ORIGINAL_DATA_MANAGER_H

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <string>
#include "data_loader.h"
#include "spatiotemporal_data.h"

/**
 * @brief Original data manager singleton class
 * 
 * Manages original spatiotemporal data using PostgreSQL large object storage
 * with dual key (key1, key2) indexing. Uses singleton pattern for consistent
 * global access.
 */
class OriginalDataManager {
public:
    static constexpr const char* TABLE_NAME = "original_data";
    
    // File-based storage as backup option
    // static BinaryKVStorage bs;
    
    /**
     * @brief Get singleton instance
     * @return OriginalDataManager& singleton reference
     */
    static OriginalDataManager& getInstance();
    
    // Delete copy constructor and assignment operations
    OriginalDataManager(const OriginalDataManager&) = delete;
    OriginalDataManager& operator=(const OriginalDataManager&) = delete;
    OriginalDataManager(OriginalDataManager&&) = delete;
    OriginalDataManager& operator=(OriginalDataManager&&) = delete;
    
    /**
     * @brief Clear all data in the table
     */
    void clearTable();
    
    /**
     * @brief Write original data to database
     * @param key1 first key
     * @param key2 second key
     * @param data spatiotemporal data vector
     */
    void writeOriginalDataToDatabase(int key1, int key2, const std::vector<SpatioTemporalData>& data);
    
    /**
     * @brief Update original data in database
     * @param key1 first key
     * @param key2 second key
     * @param newData new spatiotemporal data vector
     */
    void updateOriginalDataInDatabase(int key1, int key2, const std::vector<SpatioTemporalData>& newData);
    
    /**
     * @brief Load original data from database by dual key
     * @param key1 first key
     * @param key2 second key
     * @return std::vector<SpatioTemporalData> loaded data
     */
    std::vector<SpatioTemporalData> loadOriginalDataFromDatabase(int key1, int key2);
    
    /**
     * @brief Load all original data from database by first key
     * @param key1 first key
     * @return std::unordered_map<int, std::vector<SpatioTemporalData>> map of second key to data
     */
    std::unordered_map<int, std::vector<SpatioTemporalData>> loadOriginalDataFromDatabase(int key1);
    
    /**
     * @brief Load all original data from database
     * @return std::unordered_map<int, std::unordered_map<int, std::vector<SpatioTemporalData>>> all data
     */
    std::unordered_map<int, std::unordered_map<int, std::vector<SpatioTemporalData>>> loadAllOriginalData();
    
    /**
     * @brief Get total count of records in table
     * @return std::int64_t total record count
     */
    std::int64_t size();
    
    /**
     * @brief Load all keys from database
     * @return std::vector<std::pair<int, int>> vector of (key1, key2) pairs
     */
    std::vector<std::pair<int, int>> loadAllKeysFromDatabase();

private:
    /**
     * @brief Private constructor for singleton pattern
     */
    OriginalDataManager() = default;
    
    /**
     * @brief Private destructor
     */
    ~OriginalDataManager() = default;
};

// Global singleton instance
extern OriginalDataManager& originalDataManager;

#endif // ORIGINAL_DATA_MANAGER_H