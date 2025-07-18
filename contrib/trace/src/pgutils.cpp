#include "../include/safe_header.h"
#include "../include/pgutils.h"
#include <mutex>

/**
 * @file pgutils.cpp
 * @brief PostgreSQL Database Utilities Implementation
 * 
 * This file implements the PostgreSQLUtils singleton class providing comprehensive
 * database operations for the PostgreSQL extension. The class encapsulates all
 * PostgreSQL-specific operations including SQL execution, binary data management,
 * large object operations, memory management, and array processing.
 * 
 * @author PostgreSQL Extension Development Team
 * @version 1.0
 * 
 * Key Features:
 * - Thread-safe singleton pattern
 * - Comprehensive error handling
 * - Memory context management
 * - Binary data and large object support
 * - Dual-key table operations
 * - Array processing utilities
 */

// ============================================================================
// SINGLETON MANAGEMENT
// ============================================================================

/**
 * @brief Get singleton instance of PostgreSQLUtils
 * 
 * Implements thread-safe singleton pattern using C++11 static initialization.
 * The instance is created on first access and persists for the lifetime of
 * the process. This ensures all database operations use the same utility
 * instance with shared state tracking.
 * 
 * @return PostgreSQLUtils& Reference to the singleton instance
 * 
 * @note Thread-safe due to C++11 guaranteed static initialization
 * @see https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables
 * 
 * @example
 * PostgreSQLUtils& pgutils = PostgreSQLUtils::getInstance();
 * pgutils.executeSQL("CREATE TABLE test (id INT)");
 */
PostgreSQLUtils& PostgreSQLUtils::getInstance() {
    static PostgreSQLUtils instance;
    return instance;
}

/**
 * @brief Global convenience reference to PostgreSQL utilities singleton
 * 
 * Provides easy access to the PostgreSQLUtils instance without requiring
 * repeated getInstance() calls. This global reference is initialized once
 * and can be used throughout the codebase for database operations.
 * 
 * @note This is a reference, not a pointer, ensuring it's always valid
 * 
 * @example
 * pgutils.executeSQL("INSERT INTO table VALUES (1, 'data')");
 */
PostgreSQLUtils& pgutils = PostgreSQLUtils::getInstance();

// ============================================================================
// BASIC SQL OPERATIONS
// ============================================================================

/**
 * @brief Execute SQL statement without returning results
 * 
 * Executes DDL/DML statements like CREATE, INSERT, UPDATE, DELETE that don't
 * require result processing. Provides thread-safe access through mutex locking
 * and comprehensive error handling with automatic SPI connection management.
 * 
 * @param sql SQL statement to execute (null-terminated C string)
 * 
 * @throws PostgreSQL ERROR if:
 *         - SPI connection fails
 *         - SQL execution fails
 *         - Invalid SQL syntax
 * 
 * @note Thread-safe through internal mutex locking
 * @note Automatically manages SPI connect/finish lifecycle
 * @note Logs execution count and SQL statement for debugging
 * 
 * @example
 * pgutils.executeSQL("CREATE TABLE users (id SERIAL PRIMARY KEY, name TEXT)");
 * pgutils.executeSQL("INSERT INTO users (name) VALUES ('John Doe')");
 * pgutils.executeSQL("UPDATE users SET name = 'Jane Doe' WHERE id = 1");
 */
void PostgreSQLUtils::executeSQL(const char* sql) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    // Increment call count for debugging and monitoring
    sql_call_count_++;
    
    elog(INFO, "PostgreSQLUtils::executeSQL #%d: %s", sql_call_count_, sql);

    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    int ret = SPI_exec(sql, 0);
    if (ret < 0) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_exec failed: %s", sql)));
    }
    
    SPI_finish();
}

/**
 * @brief Execute SELECT statement and return result table
 * 
 * Executes SELECT queries and returns a complete result set as SPITupleTable.
 * The returned data is copied to the caller's memory context, ensuring it
 * remains valid after SPI operations complete. Handles multi-row results
 * with proper memory management.
 * 
 * @param sql SELECT statement to execute
 * @return SPITupleTable* Pointer to result table, or NULL if no results
 *         - Contains: tupdesc (column descriptions), vals (row data), alloced (row count)
 *         - Memory allocated in caller's context using SPI_palloc
 * 
 * @throws PostgreSQL ERROR if:
 *         - SPI connection fails
 *         - SQL execution fails
 *         - Memory allocation fails
 * 
 * @note Result data copied to caller context for persistence
 * @note Caller responsible for memory management of returned pointer
 * @note Thread-safe through internal mutex locking
 * 
 * @example
 * SPITupleTable* results = pgutils.executeSQLSelect("SELECT id, name FROM users");
 * if (results && results->alloced > 0) {
 *     for (int i = 0; i < results->alloced; i++) {
 *         // Process results->vals[i] using results->tupdesc
 *     }
 * }
 */
SPITupleTable* PostgreSQLUtils::executeSQLSelect(const char* sql) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    int ret = SPI_exec(sql, 0);
    if (ret < 0) {
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_exec failed: %s", sql)));
    }
    
    SPITupleTable* result = NULL;
    
    if (SPI_tuptable && SPI_processed > 0) {
        // Use SPI_palloc to allocate SPITupleTable in caller context
        result = (SPITupleTable*) SPI_palloc(sizeof(SPITupleTable));
        
        // Copy basic information
        result->alloced = SPI_processed;
        // result->free = 0;  // 'free' member removed in newer PostgreSQL versions
        
        // Copy TupleDesc to caller context
        result->tupdesc = CreateTupleDescCopy(SPI_tuptable->tupdesc);
        
        // Allocate HeapTuple array
        result->vals = (HeapTuple*) SPI_palloc(SPI_processed * sizeof(HeapTuple));
        
        // Copy each HeapTuple to caller context
        for (uint64 i = 0; i < SPI_processed; i++) {
            result->vals[i] = SPI_copytuple(SPI_tuptable->vals[i]);
        }
    }
    
    // Complete SPI operation
    SPI_finish();
    
    return result;
}

// ============================================================================
// BINARY DATA OPERATIONS (SINGLE KEY)
// ============================================================================

/**
 * @brief Insert binary data into single-key table
 * 
 * Inserts binary data into a table with schema: (key INT, data BYTEA).
 * Uses PostgreSQL's prepared statements for optimal performance and security.
 * Automatically handles binary data encoding and VARHDRSZ header management.
 * 
 * @param table_name Name of target table (must exist with correct schema)
 * @param key_value Integer key for the record
 * @param binary_data Pointer to binary data buffer
 * @param binary_size Size of binary data in bytes
 * 
 * @throws PostgreSQL ERROR if:
 *         - SPI connection fails
 *         - Statement preparation fails
 *         - INSERT execution fails
 *         - Memory allocation fails
 * 
 * @note Thread-safe through internal mutex locking
 * @note Logs detailed execution information for debugging
 * @note Memory automatically managed through PostgreSQL allocator
 * 
 * @example
 * std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
 * pgutils.executeBinaryInsert("blob_table", 42, data.data(), data.size());
 */
void PostgreSQLUtils::executeBinaryInsert(const char* table_name, int key_value, 
                                         const void* binary_data, size_t binary_size) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    // Increment call count for monitoring
    insert_call_count_++;
    
    // Record call information for debugging
    elog(INFO, "PostgreSQLUtils::executeBinaryInsert #%d: table=%s, key=%d, data_size=%zu bytes", 
         insert_call_count_, table_name, key_value, binary_size);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build parameterized INSERT statement for security
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "INSERT INTO %s (key, data) VALUES ($1, $2)", table_name);
    
    // Prepare parameters with proper PostgreSQL types
    Oid argtypes[2] = {INT4OID, BYTEAOID};
    Datum values[2];
    char nulls[2] = {' ', ' '};
    
    // Set integer key parameter
    values[0] = Int32GetDatum(key_value);
    
    // Create PostgreSQL bytea from binary data
    bytea *binary_bytea = (bytea *) palloc(VARHDRSZ + binary_size);
    SET_VARSIZE(binary_bytea, VARHDRSZ + binary_size);
    memcpy(VARDATA(binary_bytea), binary_data, binary_size);
    values[1] = PointerGetDatum(binary_bytea);
    
    // Log memory allocation details
    size_t allocated_size = VARHDRSZ + binary_size;
    elog(INFO, "PostgreSQLUtils::executeBinaryInsert #%d: allocated bytea size=%zu bytes (header=%d + data=%zu)", 
         insert_call_count_, allocated_size, VARHDRSZ, binary_size);
    
    // Prepare and execute the statement
    SPIPlanPtr plan = SPI_prepare(sql_buf.data, 2, argtypes);
    if (plan == NULL) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed")));
    }
    
    int ret = SPI_execute_plan(plan, values, nulls, false, 0);
    if (ret != SPI_OK_INSERT) {
        SPI_freeplan(plan);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for INSERT")));
    }
    
    // Log successful operation
    elog(INFO, "PostgreSQLUtils::executeBinaryInsert #%d: SUCCESS - inserted %zu bytes into %s[key=%d]", 
         insert_call_count_, binary_size, table_name, key_value);
    
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
}

/**
 * @brief Select binary data by single key
 * 
 * Retrieves binary data from single-key table using parameterized query.
 * Returns data allocated in caller's memory context for persistence.
 * Handles BYTEA decoding and proper memory allocation automatically.
 * 
 * @param table_name Name of source table
 * @param key_value Integer key to search for
 * @return BinarySelectResult* Pointer to result structure containing:
 *         - data: Pointer to binary data (SPI_palloc allocated)
 *         - size: Size of binary data in bytes
 *         Returns NULL if no data found
 * 
 * @throws PostgreSQL ERROR if:
 *         - SPI connection fails
 *         - Statement preparation fails
 *         - SELECT execution fails
 * 
 * @note Memory allocated in caller context using SPI_palloc
 * @note Thread-safe through internal mutex locking
 * @note Extensive logging for debugging and monitoring
 * 
 * @example
 * BinarySelectResult* result = pgutils.executeBinarySelect("blob_table", 42);
 * if (result && result->data) {
 *     // Process result->data with size result->size
 *     std::vector<uint8_t> data((uint8_t*)result->data, 
 *                               (uint8_t*)result->data + result->size);
 * }
 */
BinarySelectResult* PostgreSQLUtils::executeBinarySelect(const char* table_name, int key_value) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    // Log function entry with parameters
    elog(INFO, "PostgreSQLUtils::executeBinarySelect: table=%s, key=%d", table_name, key_value);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(ERROR, "PostgreSQLUtils::executeBinarySelect: SPI_connect failed for table=%s, key=%d", 
             table_name, key_value);
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build parameterized SELECT statement
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT data FROM %s WHERE key = $1", table_name);
    
    elog(INFO, "PostgreSQLUtils::executeBinarySelect: executing SQL: %s with key=%d", 
         sql_buf.data, key_value);
    
    // Prepare parameters
    Oid argtypes[1] = {INT4OID};
    Datum values[1];
    char nulls[1] = {' '};
    
    // Set key value parameter
    values[0] = Int32GetDatum(key_value);
    
    // Prepare and execute plan
    SPIPlanPtr plan = SPI_prepare(sql_buf.data, 1, argtypes);
    if (plan == NULL) {
        elog(ERROR, "PostgreSQLUtils::executeBinarySelect: SPI_prepare failed for table=%s, key=%d", 
             table_name, key_value);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed")));
    }
    
    int ret = SPI_execute_plan(plan, values, nulls, true, 0);
    if (ret != SPI_OK_SELECT) {
        elog(ERROR, "PostgreSQLUtils::executeBinarySelect: SPI_execute_plan failed (ret=%d) for table=%s, key=%d", 
             ret, table_name, key_value);
        SPI_freeplan(plan);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for SELECT")));
    }
    
    elog(INFO, "PostgreSQLUtils::executeBinarySelect: query executed successfully, processed=%lu rows for table=%s, key=%d", 
         SPI_processed, table_name, key_value);
    
    BinarySelectResult* result = NULL;
    
    if (SPI_processed > 0) {
        // Get first result tuple
        HeapTuple tuple = SPI_tuptable->vals[0];
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        bool isnull;
        Datum datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
        
        if (!isnull) {
            // Extract binary data from BYTEA
            bytea *binary_bytea = DatumGetByteaP(datum);
            size_t data_size = VARSIZE(binary_bytea) - VARHDRSZ;
            
            elog(INFO, "PostgreSQLUtils::executeBinarySelect: found data with size=%zu bytes for table=%s, key=%d", 
                 data_size, table_name, key_value);
            
            // Allocate result structure in caller context
            result = (BinarySelectResult*) SPI_palloc(sizeof(BinarySelectResult));
            result->size = data_size;
            
            // Allocate and copy data in caller context
            result->data = SPI_palloc(data_size);
            memcpy(result->data, VARDATA(binary_bytea), data_size);
            
            elog(INFO, "PostgreSQLUtils::executeBinarySelect: successfully allocated and copied %zu bytes for table=%s, key=%d", 
                 data_size, table_name, key_value);
        } else {
            elog(WARNING, "PostgreSQLUtils::executeBinarySelect: data column is NULL for table=%s, key=%d", 
                 table_name, key_value);
        }
    } else {
        elog(WARNING, "PostgreSQLUtils::executeBinarySelect: no data found for table=%s, key=%d", 
             table_name, key_value);
    }
    
    // Clean up SPI resources
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
    
    if (result != NULL) {
        elog(INFO, "PostgreSQLUtils::executeBinarySelect: SUCCESS - returning %zu bytes for table=%s, key=%d", 
             result->size, table_name, key_value);
    } else {
        elog(INFO, "PostgreSQLUtils::executeBinarySelect: SUCCESS - returning NULL result for table=%s, key=%d", 
             table_name, key_value);
    }
    
    return result;
}

/**
 * @brief Select all binary data from single-key table
 * 
 * Retrieves all records from a single-key table, returning complete dataset
 * with keys and associated binary data. Useful for bulk operations and
 * data migration. Allocates all data in caller's memory context.
 * 
 * @param table_name Name of source table
 * @return BinarySelectAllResult* Pointer to result structure containing:
 *         - count: Number of records retrieved
 *         - keys: Array of integer keys
 *         - data_array: Array of binary data pointers
 *         - size_array: Array of data sizes
 *         Returns NULL if table is empty
 * 
 * @throws PostgreSQL ERROR if:
 *         - SPI connection fails
 *         - SELECT execution fails
 *         - Memory allocation fails
 * 
 * @note All memory allocated in caller context for persistence
 * @note Thread-safe through internal mutex locking
 * @note Handles NULL data values gracefully
 * 
 * @example
 * BinarySelectAllResult* results = pgutils.executeBinarySelectAll("blob_table");
 * if (results) {
 *     for (int i = 0; i < results->count; i++) {
 *         int key = results->keys[i];
 *         void* data = results->data_array[i];
 *         size_t size = results->size_array[i];
 *         // Process each record
 *     }
 * }
 */
BinarySelectAllResult* PostgreSQLUtils::executeBinarySelectAll(const char* table_name) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement for all records
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT key, data FROM %s", table_name);
    
    int ret = SPI_exec(sql_buf.data, 0);
    if (ret != SPI_OK_SELECT) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_exec failed for SELECT ALL")));
    }
    
    BinarySelectAllResult* result = NULL;
    
    if (SPI_processed > 0) {
        // Allocate result structure in caller context
        result = (BinarySelectAllResult*) SPI_palloc(sizeof(BinarySelectAllResult));
        result->count = SPI_processed;
        
        // Allocate arrays for results
        result->keys = (int*) SPI_palloc(result->count * sizeof(int));
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        // Process each result tuple
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            
            // Extract key value
            bool isnull;
            Datum key_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            result->keys[i] = DatumGetInt32(key_datum);
            
            // Extract binary data
            Datum data_datum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
            if (!isnull) {
                bytea *binary_bytea = DatumGetByteaP(data_datum);
                result->size_array[i] = VARSIZE(binary_bytea) - VARHDRSZ;
                
                // Allocate and copy data
                result->data_array[i] = SPI_palloc(result->size_array[i]);
                memcpy(result->data_array[i], VARDATA(binary_bytea), result->size_array[i]);
            } else {
                result->data_array[i] = NULL;
                result->size_array[i] = 0;
            }
        }
    }
    
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
}

// ============================================================================
// BINARY DATA OPERATIONS (DUAL KEY)
// ============================================================================

/**
 * @brief Insert binary data into dual-key table
 * 
 * Inserts binary data into a table with schema: (key1 INT, key2 INT, data BYTEA).
 * Supports composite key operations for hierarchical data structures and
 * multi-dimensional indexing. Uses prepared statements for security and performance.
 * 
 * @param table_name Name of target table
 * @param key1_value First key component (e.g., user ID)
 * @param key2_value Second key component (e.g., document type)
 * @param binary_data Pointer to binary data buffer
 * @param binary_size Size of binary data in bytes
 * 
 * @throws PostgreSQL ERROR if:
 *         - SPI connection fails
 *         - Statement preparation fails
 *         - INSERT execution fails
 * 
 * @note Thread-safe through internal mutex locking
 * @note Requires table with composite primary key (key1, key2)
 * @note Automatically handles BYTEA encoding
 * 
 * @example
 * // Insert user profile data (user_id=100, profile_type=1)
 * std::string profile_json = "{\"name\":\"John\",\"age\":30}";
 * pgutils.executeBinaryInsertDualKey("user_profiles", 100, 1, 
 *                                   profile_json.data(), profile_json.size());
 */
void PostgreSQLUtils::executeBinaryInsertDualKey(const char* table_name, int key1_value, int key2_value,
                                                const void* binary_data, size_t binary_size) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build parameterized INSERT statement
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "INSERT INTO %s (key1, key2, data) VALUES ($1, $2, $3)", table_name);
    
    // Prepare parameters for dual-key table
    Oid argtypes[3] = {INT4OID, INT4OID, BYTEAOID};
    Datum values[3];
    char nulls[3] = {' ', ' ', ' '};
    
    // Set key parameters
    values[0] = Int32GetDatum(key1_value);
    values[1] = Int32GetDatum(key2_value);
    
    // Create BYTEA from binary data
    bytea *binary_bytea = (bytea *) palloc(VARHDRSZ + binary_size);
    SET_VARSIZE(binary_bytea, VARHDRSZ + binary_size);
    memcpy(VARDATA(binary_bytea), binary_data, binary_size);
    values[2] = PointerGetDatum(binary_bytea);
    
    // Execute prepared statement
    SPIPlanPtr plan = SPI_prepare(sql_buf.data, 3, argtypes);
    if (plan == NULL) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed")));
    }
    
    int ret = SPI_execute_plan(plan, values, nulls, false, 0);
    if (ret != SPI_OK_INSERT) {
        SPI_freeplan(plan);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for INSERT")));
    }
    
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
}

/**
 * @brief Upsert binary data in dual-key table
 * 
 * Performs INSERT with ON CONFLICT DO UPDATE for dual-key tables.
 * If the key combination exists, updates the data; otherwise inserts new record.
 * Ideal for maintaining latest data versions and avoiding duplicate key errors.
 * 
 * @param table_name Name of target table
 * @param key1_value First key component
 * @param key2_value Second key component  
 * @param binary_data Pointer to binary data buffer
 * @param binary_size Size of binary data in bytes
 * 
 * @throws PostgreSQL ERROR if:
 *         - SPI connection fails
 *         - Statement preparation fails
 *         - UPSERT execution fails
 * 
 * @note Requires table with UNIQUE constraint on (key1, key2)
 * @note Thread-safe through internal mutex locking
 * @note More efficient than separate SELECT + INSERT/UPDATE logic
 * 
 * @example
 * // Update user settings, creating if not exists
 * std::string settings = "{\"theme\":\"dark\",\"notifications\":true}";
 * pgutils.executeBinaryUpsertDualKey("user_settings", 100, 1,
 *                                   settings.data(), settings.size());
 */
void PostgreSQLUtils::executeBinaryUpsertDualKey(const char* table_name, int key1_value, int key2_value,
                                                const void* binary_data, size_t binary_size) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build UPSERT statement with conflict resolution
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, 
        "INSERT INTO %s (key1, key2, data) VALUES ($1, $2, $3) "
        "ON CONFLICT (key1, key2) DO UPDATE SET data = $3", table_name);
    
    // Prepare parameters
    Oid argtypes[3] = {INT4OID, INT4OID, BYTEAOID};
    Datum values[3];
    char nulls[3] = {' ', ' ', ' '};
    
    // Set key parameters
    values[0] = Int32GetDatum(key1_value);
    values[1] = Int32GetDatum(key2_value);
    
    // Create BYTEA from binary data
    bytea *binary_bytea = (bytea *) palloc(VARHDRSZ + binary_size);
    SET_VARSIZE(binary_bytea, VARHDRSZ + binary_size);
    memcpy(VARDATA(binary_bytea), binary_data, binary_size);
    values[2] = PointerGetDatum(binary_bytea);
    
    // Execute prepared statement
    SPIPlanPtr plan = SPI_prepare(sql_buf.data, 3, argtypes);
    if (plan == NULL) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed")));
    }
    
    int ret = SPI_execute_plan(plan, values, nulls, false, 0);
    if (ret != SPI_OK_INSERT && ret != SPI_OK_UPDATE) {
        SPI_freeplan(plan);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for UPSERT")));
    }
    
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
}

/**
 * @brief Select binary data by dual key
 * 
 * Retrieves binary data using both key components for exact record matching.
 * Returns single result for the specific key combination. Useful for
 * retrieving specific items from hierarchical or categorized data structures.
 * 
 * @param table_name Name of source table
 * @param key1_value First key component to match
 * @param key2_value Second key component to match
 * @return BinarySelectResult* Pointer to result structure, or NULL if not found
 * 
 * @throws PostgreSQL ERROR if:
 *         - SPI connection fails
 *         - Statement preparation fails
 *         - SELECT execution fails
 * 
 * @note Returns only the first matching record (should be unique)
 * @note Memory allocated in caller context using SPI_palloc
 * @note Thread-safe through internal mutex locking
 * 
 * @example
 * // Get specific user profile
 * BinarySelectResult* result = pgutils.executeBinarySelectByDualKey("user_profiles", 100, 1);
 * if (result && result->data) {
 *     std::string profile((char*)result->data, result->size);
 *     // Parse JSON profile data
 * }
 */
BinarySelectResult* PostgreSQLUtils::executeBinarySelectByDualKey(const char* table_name, int key1_value, int key2_value) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build parameterized SELECT statement
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT data FROM %s WHERE key1 = $1 AND key2 = $2", table_name);
    
    // Prepare parameters for dual-key lookup
    Oid argtypes[2] = {INT4OID, INT4OID};
    Datum values[2];
    char nulls[2] = {' ', ' '};
    
    // Set key parameters
    values[0] = Int32GetDatum(key1_value);
    values[1] = Int32GetDatum(key2_value);
    
    // Execute prepared statement
    SPIPlanPtr plan = SPI_prepare(sql_buf.data, 2, argtypes);
    if (plan == NULL) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed")));
    }
    
    int ret = SPI_execute_plan(plan, values, nulls, true, 0);
    if (ret != SPI_OK_SELECT) {
        SPI_freeplan(plan);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for SELECT")));
    }
    
    BinarySelectResult* result = NULL;
    
    if (SPI_processed > 0) {
        // Extract binary data from first result
        HeapTuple tuple = SPI_tuptable->vals[0];
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        bool isnull;
        Datum datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
        
        if (!isnull) {
            bytea *binary_bytea = DatumGetByteaP(datum);
            size_t data_size = VARSIZE(binary_bytea) - VARHDRSZ;
            
            // Allocate result structure in caller context
            result = (BinarySelectResult*) SPI_palloc(sizeof(BinarySelectResult));
            result->size = data_size;
            
            // Allocate and copy data
            result->data = SPI_palloc(data_size);
            memcpy(result->data, VARDATA(binary_bytea), data_size);
        }
    }
    
    // Clean up resources
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
}

/**
 * @brief Select all records matching first key component
 * 
 * Retrieves all records that match the first key, returning multiple results
 * with their associated second keys and binary data. Useful for getting all
 * items in a category or all data for a specific entity.
 * 
 * @param table_name Name of source table
 * @param key1_value First key component to match
 * @return DualKeyBinarySelectResult* Pointer to result structure containing:
 *         - count: Number of records found
 *         - key1_array: Array of first keys (all same value)
 *         - key2_array: Array of second keys
 *         - data_array: Array of binary data pointers
 *         - size_array: Array of data sizes
 *         Returns NULL if no matches found
 * 
 * @throws PostgreSQL ERROR if:
 *         - SPI connection fails
 *         - Statement preparation fails
 *         - SELECT execution fails
 * 
 * @note All arrays allocated in caller context for persistence
 * @note Thread-safe through internal mutex locking
 * @note Handles NULL data values gracefully
 * 
 * @example
 * // Get all profiles for user 100
 * DualKeyBinarySelectResult* results = pgutils.executeBinarySelectByKey1("user_profiles", 100);
 * if (results) {
 *     for (int i = 0; i < results->count; i++) {
 *         int profile_type = results->key2_array[i];
 *         void* profile_data = results->data_array[i];
 *         size_t profile_size = results->size_array[i];
 *         // Process each profile
 *     }
 * }
 */
DualKeyBinarySelectResult* PostgreSQLUtils::executeBinarySelectByKey1(const char* table_name, int key1_value) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement for partial key match
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT key2, data FROM %s WHERE key1 = $1", table_name);
    
    // Prepare parameters
    Oid argtypes[1] = {INT4OID};
    Datum values[1];
    char nulls[1] = {' '};
    
    // Set first key parameter
    values[0] = Int32GetDatum(key1_value);
    
    // Prepare and execute plan
    SPIPlanPtr plan = SPI_prepare(sql_buf.data, 1, argtypes);
    if (plan == NULL) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed")));
    }
    
    int ret = SPI_execute_plan(plan, values, nulls, true, 0);
    if (ret != SPI_OK_SELECT) {
        SPI_freeplan(plan);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for SELECT")));
    }
    
    DualKeyBinarySelectResult* result = NULL;
    
    if (SPI_processed > 0) {
        // Use SPI_palloc to allocate struct in caller context
        result = (DualKeyBinarySelectResult*) SPI_palloc(sizeof(DualKeyBinarySelectResult));
        result->count = SPI_processed;
        
        // Use SPI_palloc to allocate arrays
        result->key1_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->key2_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            
            // Get key2 value
            bool isnull;
            Datum key2_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            result->key1_array[i] = key1_value;  // key1 is all the same
            result->key2_array[i] = DatumGetInt32(key2_datum);
            
            // Get binary data
            Datum data_datum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
            if (!isnull) {
                bytea *binary_bytea = DatumGetByteaP(data_datum);
                result->size_array[i] = VARSIZE(binary_bytea) - VARHDRSZ;
                
                // Use SPI_palloc to allocate data memory
                result->data_array[i] = SPI_palloc(result->size_array[i]);
                memcpy(result->data_array[i], VARDATA(binary_bytea), result->size_array[i]);
            } else {
                result->data_array[i] = NULL;
                result->size_array[i] = 0;
            }
        }
    }
    
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
}

/**
 * @brief Select all records from dual-key table
 * 
 * Retrieves complete dataset from dual-key table, returning
 * all records with both key components and binary data. Use with
 * caution for large datasets as it loads all data into memory.
 * 
 * @param table_name Name of source table
 * @return DualKeyBinarySelectResult* Pointer to result structure containing all records
 *         Returns NULL if table is empty
 * 
 * @throws PostgreSQL ERROR if operations fail
 * 
 * @note Can return large amounts of data - use with caution
 * @note All memory allocated in caller context for persistence
 * @note Thread-safe through internal mutex locking
 * 
 * @example
 * // Export all user profile data
 * DualKeyBinarySelectResult* all_profiles = pgutils.executeBinarySelectAllDualKey("user_profiles");
 * if (all_profiles) {
 *     for (int i = 0; i < all_profiles->count; i++) {
 *         int user_id = all_profiles->key1_array[i];
 *         int profile_type = all_profiles->key2_array[i];
 *         // Export each profile to file
 *     }
 * }
 */
DualKeyBinarySelectResult* PostgreSQLUtils::executeBinarySelectAllDualKey(const char* table_name) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement for all records
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT key1, key2, data FROM %s", table_name);
    
    int ret = SPI_exec(sql_buf.data, 0);
    if (ret != SPI_OK_SELECT) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_exec failed for SELECT ALL")));
    }
    
    DualKeyBinarySelectResult* result = NULL;
    
    if (SPI_processed > 0) {
        // Use SPI_palloc to allocate struct in caller context
        result = (DualKeyBinarySelectResult*) SPI_palloc(sizeof(DualKeyBinarySelectResult));
        result->count = SPI_processed;
        
        // Use SPI_palloc to allocate arrays
        result->key1_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->key2_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            
            // Get key1 and key2 values
            bool isnull;
            Datum key1_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            result->key1_array[i] = DatumGetInt32(key1_datum);
            
            Datum key2_datum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
            result->key2_array[i] = DatumGetInt32(key2_datum);
            
            // Get binary data
            Datum data_datum = SPI_getbinval(tuple, tupdesc, 3, &isnull);
            if (!isnull) {
                bytea *binary_bytea = DatumGetByteaP(data_datum);
                result->size_array[i] = VARSIZE(binary_bytea) - VARHDRSZ;
                
                // Use SPI_palloc to allocate data memory
                result->data_array[i] = SPI_palloc(result->size_array[i]);
                memcpy(result->data_array[i], VARDATA(binary_bytea), result->size_array[i]);
            } else {
                result->data_array[i] = NULL;
                result->size_array[i] = 0;
            }
        }
    }
    
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
} 

// ============================================================================
// LARGE OBJECT OPERATIONS (SINGLE KEY)
// ============================================================================

/**
 * @brief Safely clear large object table with proper cleanup
 * 
 * Performs comprehensive cleanup of a large object table by first retrieving
 * all large object OIDs, then calling PostgreSQL's inv_drop() API to properly
 * delete both the large object data and metadata from system tables, and
 * finally clearing the user table records.
 * 
 * This is critical because simply executing "DELETE FROM table" would only
 * remove user table records but leave orphaned large objects in the system tables.
 * This function ensures proper cleanup by using PostgreSQL's standard large object API.
 * 
 * @param table_name Name of table containing lo_oid column
 * 
 * @throws PostgreSQL ERROR if:
 *         - SPI connection fails
 *         - Large object deletion fails
 *         - Table clearing fails
 * 
 * @note Thread-safe through internal mutex locking
 * @note Uses PostgreSQL's standard inv_drop() API for proper cleanup
 * @note Automatically handles transaction safety and permission checking
 * @note Logs detailed progress information for debugging
 * 
 * @warning Always use this instead of direct DELETE to prevent storage leaks
 * 
 * @example
 * // Properly clean up blob storage table
 * pgutils.executeLargeObjectClearTable("document_blobs");
 * // All large objects and table records are now properly deleted
 */
void PostgreSQLUtils::executeLargeObjectClearTable(const char* table_name) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // First query all large object OIDs
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT lo_oid FROM %s", table_name);
    
    elog(INFO, "PostgreSQLUtils::executeLargeObjectClearTable: querying OIDs from table %s", table_name);
    
    int ret = SPI_exec(sql_buf.data, 0);
    if (ret == SPI_OK_SELECT) {
        elog(INFO, "PostgreSQLUtils::executeLargeObjectClearTable: found %lu large objects to delete", SPI_processed);
        
        // Delete all large objects
        for (int i = 0; i < SPI_processed; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isnull;
            Datum datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc, 1, &isnull);
            if (!isnull) {
                Oid lo_oid = DatumGetObjectId(datum);
                elog(INFO, "PostgreSQLUtils::executeLargeObjectClearTable: deleting large object OID %u", lo_oid);
                
                // inv_drop() will automatically delete from both pg_largeobject and pg_largeobject_metadata tables
                inv_drop(lo_oid);
                
                elog(INFO, "PostgreSQLUtils::executeLargeObjectClearTable: successfully deleted large object OID %u from system tables", lo_oid);
            }
        }
    } else {
        elog(INFO, "PostgreSQLUtils::executeLargeObjectClearTable: no large objects found in table %s", table_name);
    }
    
    // Clear table
    pfree(sql_buf.data);
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "DELETE FROM %s", table_name);
    
    elog(INFO, "PostgreSQLUtils::executeLargeObjectClearTable: clearing table %s", table_name);
    
    ret = SPI_exec(sql_buf.data, 0);
    if (ret != SPI_OK_DELETE) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_exec failed for DELETE")));
    }
    
    elog(INFO, "PostgreSQLUtils::executeLargeObjectClearTable: successfully cleared table %s", table_name);
    
    pfree(sql_buf.data);
    SPI_finish();
}

/**
 * @brief Insert large binary data using PostgreSQL large objects
 * 
 * Creates a new large object for storing binary data that exceeds BYTEA
 * size limits or requires streaming access. Large objects can handle
 * data up to 4TB and provide efficient random access patterns.
 * 
 * Process:
 * 1. Create new large object with read/write permissions
 * 2. Open large object for writing
 * 3. Write binary data to large object
 * 4. Close large object
 * 5. Insert record with large object OID into user table
 * 
 * @param table_name Name of target table (schema: key INT, lo_oid OID)
 * @param key_value Integer key for the record
 * @param binary_data Pointer to binary data buffer
 * @param binary_size Size of binary data in bytes
 * 
 * @throws PostgreSQL ERROR if:
 *         - Large object creation fails
 *         - Large object operations fail
 *         - Record insertion fails
 *         - SPI operations fail
 * 
 * @note Thread-safe through internal mutex locking
 * @note Automatically cleans up large object on any failure
 * @note Uses PostgreSQL's transaction-safe large object API
 * @note More efficient than BYTEA for large data (>1MB)
 * 
 * @example
 * // Store large document (>1MB)
 * std::ifstream file("large_document.pdf", std::ios::binary);
 * std::vector<char> document_data((std::istreambuf_iterator<char>(file)),
 *                                 std::istreambuf_iterator<char>());
 * pgutils.executeLargeObjectInsert("documents", 12345, 
 *                                 document_data.data(), document_data.size());
 */
void PostgreSQLUtils::executeLargeObjectInsert(const char* table_name, int key_value,
                                              const void* binary_data, size_t binary_size) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Create new large object with read/write permissions
    Oid lo_oid = inv_create(INV_READ | INV_WRITE);
    if (lo_oid == InvalidOid) {
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("lo_creat failed")));
    }
    
    // Open large object
    LargeObjectDesc *lobj_desc = inv_open(lo_oid, INV_WRITE, CurrentMemoryContext);
    if (lobj_desc == NULL) {
        inv_drop(lo_oid);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("lo_open failed")));
    }
    
    // Write data
    int nbytes = inv_write(lobj_desc, (char*)binary_data, binary_size);
    if (nbytes != binary_size) {
        inv_close(lobj_desc);
        inv_drop(lo_oid);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("lo_write failed")));
    }
    
    // Close large object
    inv_close(lobj_desc);
    
    // Insert record
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "INSERT INTO %s (key, lo_oid) VALUES ($1, $2)", table_name);
    
    // Prepare parameters
    Oid argtypes[2] = {INT4OID, OIDOID};
    Datum values[2];
    char nulls[2] = {' ', ' '};
    
    values[0] = Int32GetDatum(key_value);
    values[1] = ObjectIdGetDatum(lo_oid);
    
    // Prepare and execute plan
    SPIPlanPtr plan = SPI_prepare(sql_buf.data, 2, argtypes);
    if (plan == NULL) {
        inv_drop(lo_oid);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed")));
    }
    
    int ret = SPI_execute_plan(plan, values, nulls, false, 0);
    if (ret != SPI_OK_INSERT) {
        inv_drop(lo_oid);
        SPI_freeplan(plan);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for INSERT")));
    }
    
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
}

/**
 * @brief Select large object data by key
 * 
 * Retrieves large object data by looking up the OID in the user table,
 * then reading the complete large object content into memory. Handles
 * large data efficiently with proper memory allocation in caller context.
 * 
 * Process:
 * 1. Query user table for large object OID by key
 * 2. Open large object for reading
 * 3. Determine large object size
 * 4. Allocate memory and read complete data
 * 5. Close large object and return data
 * 
 * @param table_name Name of source table
 * @param key_value Integer key to search for
 * @return LargeObjectSelectResult* Pointer to result structure containing:
 *         - count: Number of large objects found (should be 1)
 *         - data_array: Array of data pointers
 *         - size_array: Array of data sizes
 *         Returns NULL if key not found
 * 
 * @throws PostgreSQL ERROR if large object operations fail
 * 
 * @note Memory allocated in caller context using SPI_palloc
 * @note Thread-safe through internal mutex locking
 * @note Can handle very large objects (up to 4TB)
 * @note Data persists after function returns
 * 
 * @example
 * LargeObjectSelectResult* result = pgutils.executeLargeObjectSelectByKey("documents", 12345);
 * if (result && result->count > 0 && result->data_array[0]) {
 *     size_t doc_size = result->size_array[0];
 *     void* doc_data = result->data_array[0];
 *     // Process large document data
 *     std::ofstream outfile("retrieved_doc.pdf", std::ios::binary);
 *     outfile.write((char*)doc_data, doc_size);
 * }
 */
LargeObjectSelectResult* PostgreSQLUtils::executeLargeObjectSelectByKey(const char* table_name, int key_value) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement to get large object OID
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT lo_oid FROM %s WHERE key = $1", table_name);
    
    // Prepare parameters
    Oid argtypes[1] = {INT4OID};
    Datum values[1];
    char nulls[1] = {' '};
    
    // Set key value parameter
    values[0] = Int32GetDatum(key_value);
    
    // Prepare and execute plan
    SPIPlanPtr plan = SPI_prepare(sql_buf.data, 1, argtypes);
    if (plan == NULL) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed")));
    }
    
    int ret = SPI_execute_plan(plan, values, nulls, true, 0);
    if (ret != SPI_OK_SELECT) {
        SPI_freeplan(plan);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for SELECT")));
    }
    
    LargeObjectSelectResult* result = NULL;
    
    if (SPI_processed > 0) {
        // Allocate result structure in caller context
        result = (LargeObjectSelectResult*) SPI_palloc(sizeof(LargeObjectSelectResult));
        result->count = SPI_processed;
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        // Process each large object (typically just one)
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isnull;
            Datum datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc, 1, &isnull);
            
            if (!isnull) {
                Oid lo_oid = DatumGetObjectId(datum);
                
                // Open large object for reading
                LargeObjectDesc *lobj_desc = inv_open(lo_oid, INV_READ, CurrentMemoryContext);
                if (lobj_desc == NULL) {
                    SPI_freeplan(plan);
                    pfree(sql_buf.data);
                    SPI_finish();
                    ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                                   errmsg("lo_open failed for OID %u", lo_oid)));
                }
                
                // Get large object size
                int64 lo_size = inv_seek(lobj_desc, 0, SEEK_END);
                inv_seek(lobj_desc, 0, SEEK_SET);
                
                if (lo_size < 0) {
                    inv_close(lobj_desc);
                    SPI_freeplan(plan);
                    pfree(sql_buf.data);
                    SPI_finish();
                    ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                                   errmsg("lo_lseek64 failed for OID %u", lo_oid)));
                }
                
                // Allocate memory and read data
                result->size_array[i] = lo_size;
                result->data_array[i] = SPI_palloc(lo_size);
                
                int nbytes = inv_read(lobj_desc, (char*)result->data_array[i], lo_size);
                if (nbytes != lo_size) {
                    inv_close(lobj_desc);
                    SPI_freeplan(plan);
                    pfree(sql_buf.data);
                    SPI_finish();
                    ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                                   errmsg("lo_read failed for OID %u", lo_oid)));
                }
                
                inv_close(lobj_desc);
            } else {
                result->data_array[i] = NULL;
                result->size_array[i] = 0;
            }
        }
    }
    
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
}

// ============================================================================
// LARGE OBJECT OPERATIONS (DUAL KEY)
// ============================================================================

/**
 * @brief Insert large binary data using dual-key table
 * 
 * Creates large object and inserts record into dual-key table with schema:
 * (key1 INT, key2 INT, lo_oid OID). Combines the benefits of large object
 * storage with hierarchical key organization for complex data structures.
 * 
 * @param table_name Name of target table
 * @param key1_value First key component (e.g., user ID)
 * @param key2_value Second key component (e.g., document type)
 * @param binary_data Pointer to binary data buffer
 * @param binary_size Size of binary data in bytes
 * 
 * @throws PostgreSQL ERROR if:
 *         - Large object creation fails
 *         - Large object operations fail
 *         - Record insertion fails
 * 
 * @note Thread-safe through internal mutex locking
 * @note Automatically cleans up large object on failure
 * @note Requires table with composite primary key (key1, key2)
 * 
 * @example
 * // Store user avatar image
 * std::vector<uint8_t> avatar_data = load_image_file("avatar.jpg");
 * pgutils.executeLargeObjectInsertDualKey("user_avatars", 100, 1, 
 *                                        avatar_data.data(), avatar_data.size());
 */
void PostgreSQLUtils::executeLargeObjectInsertDualKey(const char* table_name, int key1_value, int key2_value,
                                                     const void* binary_data, size_t binary_size) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Create large object
    Oid lo_oid = inv_create(INV_READ | INV_WRITE);
    if (lo_oid == InvalidOid) {
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("lo_creat failed")));
    }
    
    // Open large object
    LargeObjectDesc *lobj_desc = inv_open(lo_oid, INV_WRITE, CurrentMemoryContext);
    if (lobj_desc == NULL) {
        inv_drop(lo_oid);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("lo_open failed")));
    }
    
    // Write data
    int nbytes = inv_write(lobj_desc, (char*)binary_data, binary_size);
    if (nbytes != binary_size) {
        inv_close(lobj_desc);
        inv_drop(lo_oid);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("lo_write failed")));
    }
    
    // Close large object
    inv_close(lobj_desc);
    
    // Insert record
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "INSERT INTO %s (key1, key2, lo_oid) VALUES ($1, $2, $3)", table_name);
    
    // Prepare parameters for dual-key table
    Oid argtypes[3] = {INT4OID, INT4OID, OIDOID};
    Datum values[3];
    char nulls[3] = {' ', ' ', ' '};
    
    values[0] = Int32GetDatum(key1_value);
    values[1] = Int32GetDatum(key2_value);
    values[2] = ObjectIdGetDatum(lo_oid);
    
    // Prepare and execute plan
    SPIPlanPtr plan = SPI_prepare(sql_buf.data, 3, argtypes);
    if (plan == NULL) {
        inv_drop(lo_oid);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed")));
    }
    
    int ret = SPI_execute_plan(plan, values, nulls, false, 0);
    if (ret != SPI_OK_INSERT) {
        inv_drop(lo_oid);
        SPI_freeplan(plan);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for INSERT")));
    }
    
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
}

/**
 * @brief Update large object data for dual-key record
 * 
 * Updates existing large object data or creates new record if key combination
 * doesn't exist. Efficiently handles large data updates by truncating and
 * rewriting the large object content rather than creating new objects.
 * 
 * Process:
 * 1. Query for existing OID
 * 2. If found, truncate and update existing large object
 * 3. If not found, create new large object and insert record
 * 4. Write new binary data to large object
 * 
 * @param table_name Name of target table
 * @param key1_value First key component
 * @param key2_value Second key component
 * @param binary_data Pointer to new binary data
 * @param binary_size Size of new binary data in bytes
 * 
 * @throws PostgreSQL ERROR if large object operations fail
 * 
 * @note Thread-safe through internal mutex locking
 * @note More efficient than delete + insert for updates
 * @note Preserves large object OID when updating existing records
 * 
 * @example
 * // Update user profile picture
 * std::vector<uint8_t> new_profile_pic = load_image("new_profile.jpg");
 * pgutils.executeLargeObjectUpdateDualKey("user_profiles", 100, 2,
 *                                        new_profile_pic.data(), new_profile_pic.size());
 */
void PostgreSQLUtils::executeLargeObjectUpdateDualKey(const char* table_name, int key1_value, int key2_value,
                                                     const void* binary_data, size_t binary_size) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // First get existing OID
    StringInfoData select_sql_buf;
    initStringInfo(&select_sql_buf);
    appendStringInfo(&select_sql_buf, "SELECT lo_oid FROM %s WHERE key1 = $1 AND key2 = $2", table_name);
    
    Oid select_argtypes[2] = {INT4OID, INT4OID};
    Datum select_values[2];
    char select_nulls[2] = {' ', ' '};
    
    select_values[0] = Int32GetDatum(key1_value);
    select_values[1] = Int32GetDatum(key2_value);
    
    SPIPlanPtr select_plan = SPI_prepare(select_sql_buf.data, 2, select_argtypes);
    if (select_plan == NULL) {
        pfree(select_sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed for SELECT")));
    }
    
    int ret = SPI_execute_plan(select_plan, select_values, select_nulls, true, 0);
    if (ret != SPI_OK_SELECT) {
        SPI_freeplan(select_plan);
        pfree(select_sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for SELECT")));
    }
    
    Oid lo_oid;
    if (SPI_processed > 0) {
        // Get existing OID
        HeapTuple tuple = SPI_tuptable->vals[0];
        bool isnull;
        Datum datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc, 1, &isnull);
        lo_oid = DatumGetObjectId(datum);
    } else {
        // Create new OID if record doesn't exist
        lo_oid = inv_create(INV_READ | INV_WRITE);
        if (lo_oid == InvalidOid) {
            SPI_freeplan(select_plan);
            pfree(select_sql_buf.data);
            SPI_finish();
            ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                           errmsg("lo_creat failed")));
        }
    }
    
    SPI_freeplan(select_plan);
    pfree(select_sql_buf.data);
    
    // Update large object data
    LargeObjectDesc *lobj_desc = inv_open(lo_oid, INV_WRITE, CurrentMemoryContext);
    if (lobj_desc == NULL) {
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("lo_open failed")));
    }
    
    // Truncate and write new data
    inv_truncate(lobj_desc, 0);
    int nbytes = inv_write(lobj_desc, (char*)binary_data, binary_size);
    if (nbytes != binary_size) {
        inv_close(lobj_desc);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("lo_write failed")));
    }
    
    inv_close(lobj_desc);
    
    // Insert or update record
    if (SPI_processed == 0) {
        // Insert new record
        StringInfoData insert_sql_buf;
        initStringInfo(&insert_sql_buf);
        appendStringInfo(&insert_sql_buf, "INSERT INTO %s (key1, key2, lo_oid) VALUES ($1, $2, $3)", table_name);
        
        Oid insert_argtypes[3] = {INT4OID, INT4OID, OIDOID};
        Datum insert_values[3];
        char insert_nulls[3] = {' ', ' ', ' '};
        
        insert_values[0] = Int32GetDatum(key1_value);
        insert_values[1] = Int32GetDatum(key2_value);
        insert_values[2] = ObjectIdGetDatum(lo_oid);
        
        SPIPlanPtr insert_plan = SPI_prepare(insert_sql_buf.data, 3, insert_argtypes);
        if (insert_plan == NULL) {
            pfree(insert_sql_buf.data);
            SPI_finish();
            ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                           errmsg("SPI_prepare failed for INSERT")));
        }
        
        ret = SPI_execute_plan(insert_plan, insert_values, insert_nulls, false, 0);
        if (ret != SPI_OK_INSERT) {
            SPI_freeplan(insert_plan);
            pfree(insert_sql_buf.data);
            SPI_finish();
            ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                           errmsg("SPI_execute_plan failed for INSERT")));
        }
        
        SPI_freeplan(insert_plan);
        pfree(insert_sql_buf.data);
    }
    
    SPI_finish();
}

/**
 * @brief Select large object data by dual key
 * 
 * Retrieves large object data using both key components for exact matching.
 * Returns the large object content for the specific key combination.
 * 
 * @param table_name Name of source table
 * @param key1_value First key component
 * @param key2_value Second key component
 * @return LargeObjectSelectResult* Result structure or NULL if not found
 * 
 * @throws PostgreSQL ERROR if large object operations fail
 * 
 * @note Memory allocated in caller context using SPI_palloc
 * @note Thread-safe through internal mutex locking
 * 
 * @example
 * LargeObjectSelectResult* result = pgutils.executeLargeObjectSelectByDualKey("user_avatars", 100, 1);
 * if (result && result->count > 0) {
 *     // Process avatar image data
 *     save_image_file("avatar.jpg", result->data_array[0], result->size_array[0]);
 * }
 */
LargeObjectSelectResult* PostgreSQLUtils::executeLargeObjectSelectByDualKey(const char* table_name, int key1_value, int key2_value) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement for dual-key lookup
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT lo_oid FROM %s WHERE key1 = $1 AND key2 = $2", table_name);
    
    // Prepare parameters for dual-key lookup
    Oid argtypes[2] = {INT4OID, INT4OID};
    Datum values[2];
    char nulls[2] = {' ', ' '};
    
    values[0] = Int32GetDatum(key1_value);
    values[1] = Int32GetDatum(key2_value);
    
    // Prepare and execute plan
    SPIPlanPtr plan = SPI_prepare(sql_buf.data, 2, argtypes);
    if (plan == NULL) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed")));
    }
    
    int ret = SPI_execute_plan(plan, values, nulls, true, 0);
    if (ret != SPI_OK_SELECT) {
        SPI_freeplan(plan);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for SELECT")));
    }
    
    LargeObjectSelectResult* result = NULL;
    
    if (SPI_processed > 0) {
        result = (LargeObjectSelectResult*) SPI_palloc(sizeof(LargeObjectSelectResult));
        result->count = SPI_processed;
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        // Read data for each large object
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isnull;
            Datum datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc, 1, &isnull);
            
            if (!isnull) {
                Oid lo_oid = DatumGetObjectId(datum);
                
                // Open large object
                LargeObjectDesc *lobj_desc = inv_open(lo_oid, INV_READ, CurrentMemoryContext);
                if (lobj_desc == NULL) {
                    SPI_freeplan(plan);
                    pfree(sql_buf.data);
                    SPI_finish();
                    ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                                   errmsg("lo_open failed for OID %u", lo_oid)));
                }
                
                // Get large object size
                int64 lo_size = inv_seek(lobj_desc, 0, SEEK_END);
                inv_seek(lobj_desc, 0, SEEK_SET);
                
                if (lo_size < 0) {
                    inv_close(lobj_desc);
                    SPI_freeplan(plan);
                    pfree(sql_buf.data);
                    SPI_finish();
                    ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                                   errmsg("lo_lseek64 failed for OID %u", lo_oid)));
                }
                
                // Allocate memory and read data
                result->size_array[i] = lo_size;
                result->data_array[i] = SPI_palloc(lo_size);
                
                int nbytes = inv_read(lobj_desc, (char*)result->data_array[i], lo_size);
                if (nbytes != lo_size) {
                    inv_close(lobj_desc);
                    SPI_freeplan(plan);
                    pfree(sql_buf.data);
                    SPI_finish();
                    ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                                   errmsg("lo_read failed for OID %u", lo_oid)));
                }
                
                inv_close(lobj_desc);
            } else {
                result->data_array[i] = NULL;
                result->size_array[i] = 0;
            }
        }
    }
    
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
}

/**
 * @brief Select all large objects matching first key component
 * 
 * Retrieves all large object data that matches the first key, returning
 * multiple results with associated second keys. Useful for getting all
 * large objects in a category or for a specific entity.
 * 
 * @param table_name Name of source table
 * @param key1_value First key component to match
 * @return DualKeyBinarySelectResult* Result structure with all matches
 * 
 * @throws PostgreSQL ERROR if large object operations fail
 * 
 * @note All memory allocated in caller context for persistence
 * @note Thread-safe through internal mutex locking
 * @note Can handle multiple large objects efficiently
 * 
 * @example
 * // Get all documents for user 100
 * DualKeyBinarySelectResult* docs = pgutils.executeLargeObjectSelectByKey1("user_documents", 100);
 * if (docs) {
 *     for (int i = 0; i < docs->count; i++) {
 *         int doc_type = docs->key2_array[i];
 *         void* doc_data = docs->data_array[i];
 *         size_t doc_size = docs->size_array[i];
 *         // Save each document to file
 *     }
 * }
 */
DualKeyBinarySelectResult* PostgreSQLUtils::executeLargeObjectSelectByKey1(const char* table_name, int key1_value) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement for partial key match
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT key2, lo_oid FROM %s WHERE key1 = $1", table_name);
    
    // Prepare parameters
    Oid argtypes[1] = {INT4OID};
    Datum values[1];
    char nulls[1] = {' '};
    
    values[0] = Int32GetDatum(key1_value);
    
    // Prepare and execute plan
    SPIPlanPtr plan = SPI_prepare(sql_buf.data, 1, argtypes);
    if (plan == NULL) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_prepare failed")));
    }
    
    int ret = SPI_execute_plan(plan, values, nulls, true, 0);
    if (ret != SPI_OK_SELECT) {
        SPI_freeplan(plan);
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_execute_plan failed for SELECT")));
    }
    
    DualKeyBinarySelectResult* result = NULL;
    
    if (SPI_processed > 0) {
        result = (DualKeyBinarySelectResult*) SPI_palloc(sizeof(DualKeyBinarySelectResult));
        result->count = SPI_processed;
        
        result->key1_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->key2_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            
            // Get key2 value
            bool isnull;
            Datum key2_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            result->key1_array[i] = key1_value;
            result->key2_array[i] = DatumGetInt32(key2_datum);
            
            // Get large object data
            Datum lo_oid_datum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
            if (!isnull) {
                Oid lo_oid = DatumGetObjectId(lo_oid_datum);
                
                // Open large object
                LargeObjectDesc *lobj_desc = inv_open(lo_oid, INV_READ, CurrentMemoryContext);
                if (lobj_desc != NULL) {
                    // Get large object size
                    int64 lo_size = inv_seek(lobj_desc, 0, SEEK_END);
                    inv_seek(lobj_desc, 0, SEEK_SET);
                    
                    if (lo_size >= 0) {
                        result->size_array[i] = lo_size;
                        result->data_array[i] = SPI_palloc(lo_size);
                        
                        int nbytes = inv_read(lobj_desc, (char*)result->data_array[i], lo_size);
                        if (nbytes != lo_size) {
                            result->data_array[i] = NULL;
                            result->size_array[i] = 0;
                        }
                    } else {
                        result->data_array[i] = NULL;
                        result->size_array[i] = 0;
                    }
                    
                    inv_close(lobj_desc);
                } else {
                    result->data_array[i] = NULL;
                    result->size_array[i] = 0;
                }
            } else {
                result->data_array[i] = NULL;
                result->size_array[i] = 0;
            }
        }
    }
    
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
}

/**
 * @brief Select all large objects from dual-key table
 * 
 * Retrieves complete dataset from dual-key large object table, returning
 * all records with both key components and large object data. Use with
 * caution for large datasets as it loads all data into memory.
 * 
 * @param table_name Name of source table
 * @return DualKeyBinarySelectResult* Complete result set or NULL if empty
 * 
 * @throws PostgreSQL ERROR if operations fail
 * 
 * @note Can consume significant memory for large datasets
 * @note All memory allocated in caller context for persistence
 * @note Thread-safe through internal mutex locking
 * 
 * @example
 * // Export all user documents for backup
 * DualKeyBinarySelectResult* all_docs = pgutils.executeLargeObjectSelectAllDualKey("user_documents");
 * if (all_docs) {
 *     for (int i = 0; i < all_docs->count; i++) {
 *         export_document_to_backup(all_docs->key1_array[i], all_docs->key2_array[i],
 *                                  all_docs->data_array[i], all_docs->size_array[i]);
 *     }
 * }
 */
DualKeyBinarySelectResult* PostgreSQLUtils::executeLargeObjectSelectAllDualKey(const char* table_name) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement for all records
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT key1, key2, lo_oid FROM %s", table_name);
    
    int ret = SPI_exec(sql_buf.data, 0);
    if (ret != SPI_OK_SELECT) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_exec failed for SELECT ALL")));
    }
    
    DualKeyBinarySelectResult* result = NULL;
    
    if (SPI_processed > 0) {
        result = (DualKeyBinarySelectResult*) SPI_palloc(sizeof(DualKeyBinarySelectResult));
        result->count = SPI_processed;
        
        result->key1_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->key2_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            
            // Get key1 and key2 values
            bool isnull;
            Datum key1_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            result->key1_array[i] = DatumGetInt32(key1_datum);
            
            Datum key2_datum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
            result->key2_array[i] = DatumGetInt32(key2_datum);
            
            // Get large object data
            Datum lo_oid_datum = SPI_getbinval(tuple, tupdesc, 3, &isnull);
            if (!isnull) {
                Oid lo_oid = DatumGetObjectId(lo_oid_datum);
                
                // Open large object
                LargeObjectDesc *lobj_desc = inv_open(lo_oid, INV_READ, CurrentMemoryContext);
                if (lobj_desc != NULL) {
                    // Get large object size
                    int64 lo_size = inv_seek(lobj_desc, 0, SEEK_END);
                    inv_seek(lobj_desc, 0, SEEK_SET);
                    
                    if (lo_size >= 0) {
                        result->size_array[i] = lo_size;
                        result->data_array[i] = SPI_palloc(lo_size);
                        
                        int nbytes = inv_read(lobj_desc, (char*)result->data_array[i], lo_size);
                        if (nbytes != lo_size) {
                            result->data_array[i] = NULL;
                            result->size_array[i] = 0;
                        }
                    } else {
                        result->data_array[i] = NULL;
                        result->size_array[i] = 0;
                    }
                    
                    inv_close(lobj_desc);
                } else {
                    result->data_array[i] = NULL;
                    result->size_array[i] = 0;
                }
            } else {
                result->data_array[i] = NULL;
                result->size_array[i] = 0;
            }
        }
    }
    
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
}

// ============================================================================
// MEMORY MANAGEMENT UTILITIES
// ============================================================================

/**
 * @brief Allocate memory in specified PostgreSQL memory context
 * 
 * Safely allocates memory within a specific PostgreSQL memory context by
 * temporarily switching contexts, allocating memory, and switching back.
 * This ensures proper memory lifecycle management and automatic cleanup
 * when contexts are destroyed.
 * 
 * PostgreSQL uses memory contexts for:
 * - Automatic cleanup on transaction/function end
 * - Hierarchical memory management
 * - Error recovery and rollback support
 * - Performance optimization through bulk deallocation
 * 
 * @param size Size of memory to allocate in bytes
 * @param context Target memory context for allocation
 * @return void* Pointer to allocated memory
 * 
 * @throws PostgreSQL ERROR if allocation fails (out of memory)
 * 
 * @note Function is thread-safe within single PostgreSQL backend
 * @note Memory automatically freed when context is destroyed
 * @note No explicit deallocation required in most cases
 * 
 * @example
 * // Allocate buffer in function's memory context
 * MemoryContext func_context = CurrentMemoryContext;
 * void* buffer = pgutils.palloc_in_context(1024, func_context);
 * // Memory automatically freed when function returns
 */
void* PostgreSQLUtils::palloc_in_context(Size size, MemoryContext context)
{
    // Memory functions are thread-safe within same PostgreSQL backend
    // No mutex needed as each backend is single-threaded
    MemoryContext old_context = MemoryContextSwitchTo(context);
    void* ptr = palloc(size);
    MemoryContextSwitchTo(old_context);
    return ptr;
}

/**
 * @brief Free memory allocated in PostgreSQL memory context
 * 
 * Safely deallocates memory that was allocated with palloc() or related
 * functions. Handles NULL pointers gracefully to prevent crashes.
 * In most PostgreSQL code, explicit freeing is unnecessary as memory
 * contexts provide automatic cleanup.
 * 
 * @param ptr Pointer to memory to free (can be NULL)
 * @param context Memory context (currently unused but kept for API consistency)
 */
void PostgreSQLUtils::pfree_in_context(void* ptr, MemoryContext context)
{
    // Check for NULL pointer to prevent crashes
    if (ptr) {
        pfree(ptr);
    }
    // Note: context parameter kept for future use and API consistency
}

/**
 * @brief Duplicate string in specified PostgreSQL memory context
 * 
 * Creates a copy of the input string using PostgreSQL's pstrdup() function
 * within the specified memory context. The resulting string is allocated
 * in the target context and will be automatically freed when that context
 * is destroyed.
 * 
 * @param str Source string to duplicate (can be NULL)
 * @param context Target memory context for the string copy
 * @return char* Pointer to duplicated string, or NULL if input was NULL
 * 
 * @throws PostgreSQL ERROR if allocation fails
 * 
 * @note Handles NULL input strings safely
 * @note Resulting string is null-terminated
 * @note Memory managed by specified context
 * @note Thread-safe within single PostgreSQL backend
 * 
 * @example
 * // Duplicate string in function context for persistence
 * MemoryContext func_context = CurrentMemoryContext;
 * char* persistent_name = pgutils.pstrdup_in_context("user_data", func_context);
 * // String persists until function context is destroyed
 */
char* PostgreSQLUtils::pstrdup_in_context(const char* str, MemoryContext context)
{
    // Handle NULL input string
    if (!str) {
        return NULL;
    }
    
    // Switch to target context, duplicate string, then switch back
    MemoryContext old_context = MemoryContextSwitchTo(context);
    char* result = pstrdup(str);
    MemoryContextSwitchTo(old_context);
    return result;
}

// ============================================================================
// ARRAY PROCESSING UTILITIES
// ============================================================================

/**
 * @brief Extract string array from PostgreSQL ArrayType
 * 
 * Comprehensive implementation for converting PostgreSQL's internal ArrayType
 * representation to a C-style string array. This function handles all the
 * complexities of PostgreSQL's type system including variable-length
 * types, NULL elements, and proper memory allocation.
 * 
 * PostgreSQL arrays are stored in a complex internal format that includes:
 * - Array metadata (dimensions, element type, etc.)
 * - Variable-length element storage
 * - NULL element bitmaps
 * - Type-specific alignment requirements
 * 
 * This function abstracts all these details and provides a simple interface
 * for working with string arrays in C code.
 * 
 * @param array PostgreSQL ArrayType containing TEXT elements
 * @param n_elements Output parameter: number of elements in the array
 * @return char** Array of string pointers (allocated with palloc)
 *         - Each string is null-terminated C string
 *         - NULL elements represented as NULL pointers
 *         - Array itself allocated with palloc
 * 
 * @throws PostgreSQL ERROR if array processing fails
 * 
 * @note All memory allocated using palloc (PostgreSQL memory management)
 * @note Handles NULL array elements gracefully
 * @note Strings are converted to null-terminated C strings
 * @note Memory automatically managed by current memory context
 * 
 * @example
 * // Process PostgreSQL text array parameter
 * ArrayType* pg_array = PG_GETARG_ARRAYTYPE_P(0);
 * int count;
 * char** strings = pgutils.extract_string_array(pg_array, &count);
 * 
 * for (int i = 0; i < count; i++) {
 *     if (strings[i]) {
 *         elog(INFO, "String %d: %s", i, strings[i]);
 *     } else {
 *         elog(INFO, "String %d: NULL", i);
 *     }
 * }
 * // Memory automatically freed when function context ends
 */
char** PostgreSQLUtils::extract_string_array(ArrayType* array, int* n_elements)
{
    // Variables for array deconstruction
    Datum* elements;     // Array of PostgreSQL Datum values
    bool* nulls;         // Array indicating which elements are NULL
    int16 typlen;        // Type length (-1 for variable-length types like TEXT)
    bool typbyval;       // Whether type is passed by value (false for TEXT)
    char typalign;       // Type alignment requirement ('i' for int alignment)
    
    // Get type information for TEXT type
    // This is required for proper array deconstruction
    get_typlenbyvalalign(TEXTOID, &typlen, &typbyval, &typalign);
    
    // Deconstruct the PostgreSQL array into constituent elements
    // This converts the internal representation to arrays of Datum values
    deconstruct_array(array, TEXTOID, typlen, typbyval, typalign,
                     &elements, &nulls, n_elements);
    
    // Allocate result array to hold string pointers
    // Uses palloc for PostgreSQL memory management
    char** result = (char**)palloc(*n_elements * sizeof(char*));
    
    // Convert each element from Datum to C string
    for (int i = 0; i < *n_elements; i++) {
        if (nulls[i]) {
            // Handle NULL elements by setting pointer to NULL
            result[i] = NULL;
        } else {
            // Convert PostgreSQL TEXT to C string
            // DatumGetTextP: extracts text* from Datum
            // text_to_cstring: converts text* to null-terminated C string
            result[i] = text_to_cstring(DatumGetTextP(elements[i]));
        }
    }
    
    return result;
}