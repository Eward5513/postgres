#ifndef PGUTILS_H
#define PGUTILS_H

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
     * @brief Insert dual key large object data
     * @param table_name table name
     * @param key1_value first key value
     * @param key2_value second key value
     * @param binary_data binary data pointer
     * @param binary_size data size
     */
    void executeLargeObjectInsertDualKey(const char* table_name, int key1_value, int key2_value,
                                        const void* binary_data, size_t binary_size);
    
    /**
     * @brief Update dual key large object data
     * @param table_name table name
     * @param key1_value first key value
     * @param key2_value second key value
     * @param binary_data binary data pointer
     * @param binary_size data size
     */
    void executeLargeObjectUpdateDualKey(const char* table_name, int key1_value, int key2_value,
                                        const void* binary_data, size_t binary_size);
    
    /**
     * @brief Query large object data by dual key
     * @param table_name table name
     * @param key1_value first key value
     * @param key2_value second key value
     * @return LargeObjectSelectResult* query result, caller needs to free memory
     */
    LargeObjectSelectResult* executeLargeObjectSelectByDualKey(const char* table_name, int key1_value, int key2_value);
    
    /**
     * @brief Query all related large object data by first key value
     * @param table_name table name
     * @param key1_value first key value
     * @return DualKeyBinarySelectResult* query result, caller needs to free memory
     */
    DualKeyBinarySelectResult* executeLargeObjectSelectByKey1(const char* table_name, int key1_value);
    
    /**
     * @brief Query all large object data in dual key table
     * @param table_name table name
     * @return DualKeyBinarySelectResult* query result, caller needs to free memory
     */
    DualKeyBinarySelectResult* executeLargeObjectSelectAllDualKey(const char* table_name);
    
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
    
    /**
     * @brief Execute PostgreSQL COPY FROM file operation
     * @param file_path Full path to the binary data file
     * @param table_name Name of target table for COPY operation
     */
    void executeCopyFromFile(const char* file_path, const char* table_name);
    
    // ============================================================================
    // Memory Management Utilities
    // ============================================================================
    
    /**
     * @brief Allocate memory in specified PostgreSQL memory context
     * 
     * This function switches to the specified memory context, allocates memory
     * using PostgreSQL's palloc(), and then switches back to the original context.
     * This ensures the allocated memory belongs to the correct memory context
     * for proper PostgreSQL memory management.
     * 
     * @param size Size of memory to allocate in bytes
     * @param context Target memory context for allocation
     * @return void* Pointer to allocated memory
     * @throws PostgreSQL error if allocation fails
     * 
     * @note The caller is responsible for ensuring the memory context is valid
     * @see pfree_in_context() for corresponding deallocation
     */
    void* palloc_in_context(Size size, MemoryContext context);
    
    /**
     * @brief Free memory allocated in PostgreSQL memory context
     * 
     * Safely frees memory that was allocated using PostgreSQL's memory management
     * functions. This function checks for null pointers before attempting to free
     * the memory to prevent crashes.
     * 
     * @param ptr Pointer to memory to free (can be NULL)
     * @param context Memory context (currently unused but kept for API consistency)
     * 
     * @note This function is safe to call with NULL pointers
     * @see palloc_in_context() for corresponding allocation
     */
    void pfree_in_context(void* ptr, MemoryContext context);
    
    /**
     * @brief Duplicate string in specified PostgreSQL memory context
     * 
     * Creates a copy of the input string in the specified memory context using
     * PostgreSQL's pstrdup() function. This ensures the string copy is properly
     * managed by PostgreSQL's memory management system.
     * 
     * @param str Source string to duplicate (can be NULL)
     * @param context Target memory context for the string copy
     * @return char* Pointer to duplicated string, or NULL if input was NULL
     * @throws PostgreSQL error if allocation fails
     * 
     * @note The returned string is automatically freed when the memory context is reset
     * @see palloc_in_context() for related memory allocation
     */
    char* pstrdup_in_context(const char* str, MemoryContext context);
    
    // ============================================================================
    // Array Processing Utilities  
    // ============================================================================
    
    /**
     * @brief Extract string array from PostgreSQL ArrayType
     * 
     * Converts a PostgreSQL ArrayType (TEXT[]) into a C-style array of strings.
     * This function handles PostgreSQL's internal array representation and
     * converts it to a format that can be easily used in C++ code.
     * 
     * The function properly handles:
     * - NULL array elements (converted to NULL pointers)
     * - Variable-length text elements
     * - Memory allocation for the result array
     * 
     * @param array PostgreSQL ArrayType containing TEXT elements
     * @param n_elements Output parameter: number of elements in the array
     * @return char** Array of string pointers (allocated with palloc)
     * @throws PostgreSQL error if array processing fails
     * 
     * @note The caller is responsible for freeing the returned array and its elements
     * @note NULL elements in the input array result in NULL pointers in the output
     * 
     * Example usage:
     * @code
     * int count;
     * char** strings = extract_string_array(pg_array, &count);
     * for (int i = 0; i < count; i++) {
     *     if (strings[i] != NULL) {
     *         // Process string
     *     }
     * }
     * // Free memory when done
     * @endcode
     */
    char** extract_string_array(ArrayType* array, int* n_elements);

private:
    /**
     * @brief Private constructor
     */
    PostgreSQLUtils() = default;
    
    /**
     * @brief Private destructor
     */
    ~PostgreSQLUtils() = default;
};

// Global instance for convenient access
extern PostgreSQLUtils& pgutils;

#endif // PGUTILS_H 