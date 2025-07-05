#include "../include/safe_header.h"
#include "../include/original_data_manager.h"
#include "../include/pgutils.h"
#include <cstring>

extern "C" {
#include "postgres.h"
#include "utils/elog.h"
}

// Static member variable definition
// BinaryKVStorage OriginalDataManager::bs("data_buffer/original");

// Global singleton instance
OriginalDataManager& originalDataManager = OriginalDataManager::getInstance();

// Singleton instance implementation
OriginalDataManager& OriginalDataManager::getInstance() {
    static OriginalDataManager instance;
    return instance;
}

void OriginalDataManager::clearTable() {
    elog(INFO, "OriginalDataManager::clearTable()");
    
    // Clear table data
    std::string sql = "TRUNCATE TABLE " + std::string(TABLE_NAME) + ";";
    pgutils.executeSQL(sql.c_str());
}

// Write data to database
void OriginalDataManager::writeOriginalDataToDatabase(int key1, int key2, const std::vector<SpatioTemporalData>& data) {
    elog(INFO, "OriginalDataManager::writeOriginalDataToDatabase(key1=%d, key2=%d, size=%zu)", 
         key1, key2, data.size());
         
    // bs.write<SpatioTemporalData>("raw_data_"+std::to_string(key1)+"_"+std::to_string(key2),data);
    // return;

    // Use SPI interface to insert large object data
    pgutils.executeLargeObjectInsertDualKey(TABLE_NAME, key1, key2, 
                                           data.data(), data.size() * sizeof(SpatioTemporalData));
    
    elog(INFO, "OriginalDataManager::writeOriginalDataToDatabase: Successfully inserted %zu bytes", 
         data.size() * sizeof(SpatioTemporalData));
}

void OriginalDataManager::updateOriginalDataInDatabase(int key1, int key2, const std::vector<SpatioTemporalData>& newData) {
    elog(INFO, "OriginalDataManager::updateOriginalDataInDatabase(key1=%d, key2=%d, size=%zu)", 
         key1, key2, newData.size());
    
    // Use SPI interface to update large object data
    pgutils.executeLargeObjectUpdateDualKey(TABLE_NAME, key1, key2, 
                                           newData.data(), newData.size() * sizeof(SpatioTemporalData));
    
    elog(INFO, "OriginalDataManager::updateOriginalDataInDatabase: Successfully updated %zu bytes", 
         newData.size() * sizeof(SpatioTemporalData));
}

// Load data from database
std::vector<SpatioTemporalData> OriginalDataManager::loadOriginalDataFromDatabase(int key1, int key2) {
    elog(INFO, "OriginalDataManager::loadOriginalDataFromDatabase(key1=%d, key2=%d)", key1, key2);
    // return bs.read<SpatioTemporalData>("raw_data_"+std::to_string(key1)+"_"+std::to_string(key2));
    std::vector<SpatioTemporalData> data;
    
    // Use SPI interface to query large object data
    LargeObjectSelectResult* result = pgutils.executeLargeObjectSelectByDualKey(TABLE_NAME, key1, key2);
    if (result != NULL && result->count > 0) {
        // Verify data size
        size_t data_size = sizeof(SpatioTemporalData);
        if (result->size_array[0] % data_size != 0) {
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                           errmsg("Binary data size does not match expected size for SpatioTemporalData")));
        }
        
        // Convert data
        int num_data = result->size_array[0] / data_size;
        const SpatioTemporalData* buffer = reinterpret_cast<const SpatioTemporalData*>(result->data_array[0]);
        data.assign(buffer, buffer + num_data);
        
        elog(INFO, "OriginalDataManager::loadOriginalDataFromDatabase: Successfully loaded %d items", num_data);
        
        // Memory is automatically freed by SPI context
    } else {
        elog(WARNING, "OriginalDataManager::loadOriginalDataFromDatabase: No data found for keys (%d, %d)", 
             key1, key2);
    }
    
    return data;
}

std::vector<std::pair<int, int>> OriginalDataManager::loadAllKeysFromDatabase() {
    std::vector<std::pair<int, int>> all_keys;
    
    elog(INFO, "OriginalDataManager::loadAllKeysFromDatabase: Loading all keys");
    
    // Use SPI to execute query
    std::string sql = "SELECT DISTINCT key1, key2 FROM " + std::string(TABLE_NAME) + ";";
    SPITupleTable* spi_result = pgutils.executeSQLSelect(sql.c_str());
    
    if (spi_result != NULL && spi_result->tupdesc != NULL && SPI_processed > 0) {
        for (uint32_t i = 0; i < SPI_processed; i++) {
            HeapTuple tuple = spi_result->vals[i];
            TupleDesc tupdesc = spi_result->tupdesc;
            
            bool isnull1, isnull2;
            Datum key1_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull1);
            Datum key2_datum = SPI_getbinval(tuple, tupdesc, 2, &isnull2);
            
            if (!isnull1 && !isnull2) {
                int key1 = DatumGetInt32(key1_datum);
                int key2 = DatumGetInt32(key2_datum);
                all_keys.push_back(std::make_pair(key1, key2));
            }
        }
        
        elog(INFO, "OriginalDataManager::loadAllKeysFromDatabase: Successfully loaded %zu key pairs", all_keys.size());
    } else {
        elog(WARNING, "OriginalDataManager::loadAllKeysFromDatabase: No keys found in database");
    }
    
    return all_keys;
}

std::int64_t OriginalDataManager::size() {
    elog(INFO, "OriginalDataManager::size()");
    
    // Use SPI to count records
    std::string sql = "SELECT COUNT(*) FROM " + std::string(TABLE_NAME) + ";";
    SPITupleTable* spi_result = pgutils.executeSQLSelect(sql.c_str());
    
    std::int64_t total_count = 0;
    if (spi_result != NULL && spi_result->tupdesc != NULL && SPI_processed > 0) {
        HeapTuple tuple = spi_result->vals[0];
        bool isnull;
        Datum count_datum = SPI_getbinval(tuple, spi_result->tupdesc, 1, &isnull);
        
        if (!isnull) {
            total_count = DatumGetInt64(count_datum);
        }
        
        elog(INFO, "OriginalDataManager::size: Total records count: %lld", total_count);
    } else {
        elog(WARNING, "OriginalDataManager::size: Failed to get count from database");
    }
    
    return total_count;
}

// Load data from database by key1
std::unordered_map<int, std::vector<SpatioTemporalData>> OriginalDataManager::loadOriginalDataFromDatabase(int key1) {
    std::unordered_map<int, std::vector<SpatioTemporalData>> key2DataMap;
    
    elog(INFO, "OriginalDataManager::loadOriginalDataFromDatabase: Loading data for key1=%d", key1);
    
    // Use SPI interface to query all related data by first key
    DualKeyBinarySelectResult* result = pgutils.executeLargeObjectSelectByKey1(TABLE_NAME, key1);
    if (result != NULL && result->count > 0) {
        size_t data_size = sizeof(SpatioTemporalData);
        
        for (int i = 0; i < result->count; i++) {
            int key2 = result->key2_array[i];
            
            if (result->data_array[i] != NULL && result->size_array[i] > 0) {
                // Verify data size
                if (result->size_array[i] % data_size != 0) {
                    elog(WARNING, "OriginalDataManager::loadOriginalDataFromDatabase: Invalid data size for key2=%d", key2);
                    continue;
                }
                
                // Convert data
                int num_data = result->size_array[i] / data_size;
                const SpatioTemporalData* buffer = reinterpret_cast<const SpatioTemporalData*>(result->data_array[i]);
                
                std::vector<SpatioTemporalData> data(num_data);
                std::memcpy(data.data(), buffer, result->size_array[i]);
                
                // Store in map
                key2DataMap[key2] = std::move(data);
            } else {
                // Create empty vector for keys with no data
                key2DataMap[key2] = std::vector<SpatioTemporalData>();
            }
        }
        
        elog(INFO, "OriginalDataManager::loadOriginalDataFromDatabase: Successfully loaded data for %d key2 values", 
             result->count);
        
        // Memory is automatically freed by SPI context
    } else {
        elog(WARNING, "OriginalDataManager::loadOriginalDataFromDatabase: No data found for key1=%d", key1);
    }
    
    return key2DataMap;
}

// Load all original data from database
std::unordered_map<int, std::unordered_map<int, std::vector<SpatioTemporalData>>> OriginalDataManager::loadAllOriginalData() {
    std::unordered_map<int, std::unordered_map<int, std::vector<SpatioTemporalData>>> all_data;
    
    elog(INFO, "OriginalDataManager::loadAllOriginalData: Loading all original data");
    
    // Use SPI interface to query all data in dual key table
    DualKeyBinarySelectResult* result = pgutils.executeLargeObjectSelectAllDualKey(TABLE_NAME);
    if (result != NULL && result->count > 0) {
        size_t data_size = sizeof(SpatioTemporalData);
        long long total_size_count = 0;
        
        for (int i = 0; i < result->count; i++) {
            int key1 = result->key1_array[i];
            int key2 = result->key2_array[i];
            
            if (result->data_array[i] != NULL && result->size_array[i] > 0) {
                // Verify data size
                if (result->size_array[i] % data_size != 0) {
                    elog(WARNING, "OriginalDataManager::loadAllOriginalData: Invalid data size for keys (%d, %d)", 
                         key1, key2);
                    continue;
                }
                
                // Convert data
                int num_data = result->size_array[i] / data_size;
                const SpatioTemporalData* buffer = reinterpret_cast<const SpatioTemporalData*>(result->data_array[i]);
                
                std::vector<SpatioTemporalData> data(num_data);
                std::memcpy(data.data(), buffer, result->size_array[i]);
                
                total_size_count += data.size();
                
                // Store in nested map
                all_data[key1][key2] = std::move(data);
            } else {
                // Create empty vector for keys with no data
                all_data[key1][key2] = std::vector<SpatioTemporalData>();
            }
        }
        
        elog(INFO, "OriginalDataManager::loadAllOriginalData: Successfully loaded %d records with total %lld data points", 
             result->count, total_size_count);
        
        // Memory is automatically freed by SPI context
    } else {
        elog(WARNING, "OriginalDataManager::loadAllOriginalData: No data found in database");
    }
    
    return all_data;
}