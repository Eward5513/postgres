#ifndef PGUTILS_H
#define PGUTILS_H

#include <mutex>
#include <cstddef>

extern "C" {
#include "postgres.h"
#include "executor/spi.h"
#include "utils/builtins.h"
}

// Binary query result structure
struct BinarySelectResult {
    void* data;
    size_t size;
};

// Binary query all results structure
struct BinarySelectAllResult {
    int count;
    int* keys;
    void** data_array;
    size_t* size_array;
};

// Large object query result structure
struct LargeObjectSelectResult {
    int count;
    void** data_array;
    size_t* size_array;
};

// Dual key binary query result structure
struct DualKeyBinarySelectResult {
    int count;
    int* key1_array;
    int* key2_array;
    void** data_array;
    size_t* size_array;
};

/**
 * @brief Thread-safe PostgreSQL database operation singleton class
 * 
 * Uses singleton pattern to ensure global unique instance, internal mutex
 * protects all SPI operations, solving SPI thread-safety issues in
 * multi-threaded environments.
 */
class PostgreSQLUtils {
public:
    /**
     * @brief Get singleton instance
     * @return PostgreSQLUtils& singleton reference
     */
    static PostgreSQLUtils& getInstance();
    
    // Delete copy constructor and assignment operations
    PostgreSQLUtils(const PostgreSQLUtils&) = delete;
    PostgreSQLUtils& operator=(const PostgreSQLUtils&) = delete;
    PostgreSQLUtils(PostgreSQLUtils&&) = delete;
    PostgreSQLUtils& operator=(PostgreSQLUtils&&) = delete;
    
    /**
     * @brief Execute SQL statement (no result returned)
     * @param sql SQL statement
     */
    void executeSQL(const char* sql);
    
    /**
     * @brief Execute SQL query and return results
     * @param sql SQL query statement
     * @return SPITupleTable* query result table
     */
    SPITupleTable* executeSQLSelect(const char* sql);
    
    /**
     * @brief Insert binary data
     * @param table_name table name
     * @param key_value key value
     * @param binary_data binary data pointer
     * @param binary_size data size
     */
    void executeBinaryInsert(const char* table_name, int key_value, 
                            const void* binary_data, size_t binary_size);
    
    /**
     * @brief Query binary data by key value
     * @param table_name table name
     * @param key_value key value
     * @return BinarySelectResult* query result, caller needs to free memory
     */
    BinarySelectResult* executeBinarySelect(const char* table_name, int key_value);
    
    /**
     * @brief Query all binary data in table
     * @param table_name table name
     * @return BinarySelectAllResult* query result, caller needs to free memory
     */
    BinarySelectAllResult* executeBinarySelectAll(const char* table_name);
    
    /**
     * @brief Clear large object table
     * @param table_name table name
     */
    void executeLargeObjectClearTable(const char* table_name);
    
    /**
     * @brief Insert large object data
     * @param table_name table name
     * @param key_value key value
     * @param binary_data binary data pointer
     * @param binary_size data size
     */
    void executeLargeObjectInsert(const char* table_name, int key_value,
                                 const void* binary_data, size_t binary_size);
    
    /**
     * @brief Query large object data by key value
     * @param table_name table name
     * @param key_value key value
     * @return LargeObjectSelectResult* query result, caller needs to free memory
     */
    LargeObjectSelectResult* executeLargeObjectSelectByKey(const char* table_name, int key_value);
    
    /**
     * @brief Insert dual key binary data
     * @param table_name table name
     * @param key1_value first key value
     * @param key2_value second key value
     * @param binary_data binary data pointer
     * @param binary_size data size
     */
    void executeBinaryInsertDualKey(const char* table_name, int key1_value, int key2_value,
                                   const void* binary_data, size_t binary_size);
    
    /**
     * @brief Update or insert dual key binary data
     * @param table_name table name
     * @param key1_value first key value
     * @param key2_value second key value
     * @param binary_data binary data pointer
     * @param binary_size data size
     */
    void executeBinaryUpsertDualKey(const char* table_name, int key1_value, int key2_value,
                                   const void* binary_data, size_t binary_size);
    
    /**
     * @brief Query binary data by dual key
     * @param table_name table name
     * @param key1_value first key value
     * @param key2_value second key value
     * @return BinarySelectResult* query result, caller needs to free memory
     */
    BinarySelectResult* executeBinarySelectByDualKey(const char* table_name, int key1_value, int key2_value);
    
    /**
     * @brief Query all related data by first key value
     * @param table_name table name
     * @param key1_value first key value
     * @return DualKeyBinarySelectResult* query result, caller needs to free memory
     */
    DualKeyBinarySelectResult* executeBinarySelectByKey1(const char* table_name, int key1_value);
    
    /**
     * @brief Query all data in dual key table
     * @param table_name table name
     * @return DualKeyBinarySelectResult* query result, caller needs to free memory
     */
    DualKeyBinarySelectResult* executeBinarySelectAllDualKey(const char* table_name);

private:
    /**
     * @brief Private constructor
     */
    PostgreSQLUtils() : sql_call_count_(0), insert_call_count_(0) {}
    
    /**
     * @brief Private destructor
     */
    ~PostgreSQLUtils() = default;
    
    /**
     * @brief Mutex to protect all SPI operations
     */
    std::mutex spi_mutex_;
    
    /**
     * @brief Call count for executeSQL function
     */
    int sql_call_count_;
    
    /**
     * @brief Call count for executeBinaryInsert function
     */
    int insert_call_count_;
};

// Global instance for convenient access
extern PostgreSQLUtils& pgutils;

#endif // PGUTILS_H 