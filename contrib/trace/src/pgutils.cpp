#include "../include/safe_header.h"
#include "../include/pgutils.h"
#include <mutex>

// PostgreSQL singleton database utilities implementation

PostgreSQLUtils& PostgreSQLUtils::getInstance() {
    static PostgreSQLUtils instance;
    return instance;
}

// Global instance definition for convenient access
PostgreSQLUtils& pgutils = PostgreSQLUtils::getInstance();

void PostgreSQLUtils::executeSQL(const char* sql) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    // Increment call count
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

void PostgreSQLUtils::executeBinaryInsert(const char* table_name, int key_value, 
                                         const void* binary_data, size_t binary_size) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    // Increment call count
    insert_call_count_++;
    
    // Record call information
    elog(INFO, "PostgreSQLUtils::executeBinaryInsert #%d: table=%s, key=%d, data_size=%zu bytes", 
         insert_call_count_, table_name, key_value, binary_size);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build INSERT statement
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "INSERT INTO %s (key, data) VALUES ($1, $2)", table_name);
    
    // Prepare parameters
    Oid argtypes[2] = {INT4OID, BYTEAOID};
    Datum values[2];
    char nulls[2] = {' ', ' '};
    
    // Set key value parameter
    values[0] = Int32GetDatum(key_value);
    
    // Set binary data parameter
    bytea *binary_bytea = (bytea *) palloc(VARHDRSZ + binary_size);
    SET_VARSIZE(binary_bytea, VARHDRSZ + binary_size);
    memcpy(VARDATA(binary_bytea), binary_data, binary_size);
    values[1] = PointerGetDatum(binary_bytea);
    
    // Record actual allocated memory size
    size_t allocated_size = VARHDRSZ + binary_size;
    elog(INFO, "PostgreSQLUtils::executeBinaryInsert #%d: allocated bytea size=%zu bytes (header=%d + data=%zu)", 
         insert_call_count_, allocated_size, VARHDRSZ, binary_size);
    
    // Prepare and execute plan
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
    
    // Record success information
    elog(INFO, "PostgreSQLUtils::executeBinaryInsert #%d: SUCCESS - inserted %zu bytes into %s[key=%d]", 
         insert_call_count_, binary_size, table_name, key_value);
    
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
}

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
    
    // Build SELECT statement
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
        // Get result
        HeapTuple tuple = SPI_tuptable->vals[0];
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        bool isnull;
        Datum datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
        
        if (!isnull) {
            bytea *binary_bytea = DatumGetByteaP(datum);
            size_t data_size = VARSIZE(binary_bytea) - VARHDRSZ;
            
            elog(INFO, "PostgreSQLUtils::executeBinarySelect: found data with size=%zu bytes for table=%s, key=%d", 
                 data_size, table_name, key_value);
            
            // Use SPI_palloc to allocate struct in caller context
            result = (BinarySelectResult*) SPI_palloc(sizeof(BinarySelectResult));
            result->size = data_size;
            
            // Use SPI_palloc to allocate data memory in caller context
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

BinarySelectAllResult* PostgreSQLUtils::executeBinarySelectAll(const char* table_name) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement
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
        // Use SPI_palloc to allocate struct in caller context
        result = (BinarySelectAllResult*) SPI_palloc(sizeof(BinarySelectAllResult));
        result->count = SPI_processed;
        
        // Use SPI_palloc to allocate main arrays
        result->keys = (int*) SPI_palloc(result->count * sizeof(int));
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            
            // Get key value
            bool isnull;
            Datum key_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            result->keys[i] = DatumGetInt32(key_datum);
            
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
    
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
}

/**
 * @brief Safely clears a large object table by deleting both table records and associated large objects
 * 
 * This function performs a complete cleanup of a large object table by:
 * 1. Querying all large object OIDs from the specified table
 * 2. Calling inv_drop() for each OID, which AUTOMATICALLY deletes data from:
 *    - pg_largeobject table (actual large object data blocks)
 *    - pg_largeobject_metadata table (large object metadata)
 * 3. Clearing all records from the user table
 * 
 * IMPORTANT: Simply executing "DELETE FROM table_name" would only remove table records
 * but leave orphaned large objects in the system tables. This function ensures proper
 * cleanup by using PostgreSQL's standard large object API.
 * 
 * @param table_name Name of the table containing lo_oid column
 * 
 * @note The inv_drop() function is PostgreSQL's standard API for large object deletion
 *       and handles all necessary system table cleanup automatically with proper
 *       transaction safety and permission checking.
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

void PostgreSQLUtils::executeLargeObjectInsert(const char* table_name, int key_value,
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

LargeObjectSelectResult* PostgreSQLUtils::executeLargeObjectSelectByKey(const char* table_name, int key_value) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement
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
        // Use SPI_palloc to allocate result struct
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

void PostgreSQLUtils::executeBinaryInsertDualKey(const char* table_name, int key1_value, int key2_value,
                                                const void* binary_data, size_t binary_size) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build INSERT statement
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "INSERT INTO %s (key1, key2, data) VALUES ($1, $2, $3)", table_name);
    
    // Prepare parameters
    Oid argtypes[3] = {INT4OID, INT4OID, BYTEAOID};
    Datum values[3];
    char nulls[3] = {' ', ' ', ' '};
    
    // Set key value parameters
    values[0] = Int32GetDatum(key1_value);
    values[1] = Int32GetDatum(key2_value);
    
    // Set binary data parameter
    bytea *binary_bytea = (bytea *) palloc(VARHDRSZ + binary_size);
    SET_VARSIZE(binary_bytea, VARHDRSZ + binary_size);
    memcpy(VARDATA(binary_bytea), binary_data, binary_size);
    values[2] = PointerGetDatum(binary_bytea);
    
    // Prepare and execute plan
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

void PostgreSQLUtils::executeBinaryUpsertDualKey(const char* table_name, int key1_value, int key2_value,
                                                const void* binary_data, size_t binary_size) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build UPSERT statement (INSERT ... ON CONFLICT ... DO UPDATE)
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, 
        "INSERT INTO %s (key1, key2, data) VALUES ($1, $2, $3) "
        "ON CONFLICT (key1, key2) DO UPDATE SET data = $3", table_name);
    
    // Prepare parameters
    Oid argtypes[3] = {INT4OID, INT4OID, BYTEAOID};
    Datum values[3];
    char nulls[3] = {' ', ' ', ' '};
    
    // Set key value parameters
    values[0] = Int32GetDatum(key1_value);
    values[1] = Int32GetDatum(key2_value);
    
    // Set binary data parameter
    bytea *binary_bytea = (bytea *) palloc(VARHDRSZ + binary_size);
    SET_VARSIZE(binary_bytea, VARHDRSZ + binary_size);
    memcpy(VARDATA(binary_bytea), binary_data, binary_size);
    values[2] = PointerGetDatum(binary_bytea);
    
    // Prepare and execute plan
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

BinarySelectResult* PostgreSQLUtils::executeBinarySelectByDualKey(const char* table_name, int key1_value, int key2_value) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT data FROM %s WHERE key1 = $1 AND key2 = $2", table_name);
    
    // Prepare parameters
    Oid argtypes[2] = {INT4OID, INT4OID};
    Datum values[2];
    char nulls[2] = {' ', ' '};
    
    // Set key value parameters
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
    
    BinarySelectResult* result = NULL;
    
    if (SPI_processed > 0) {
        // Get result
        HeapTuple tuple = SPI_tuptable->vals[0];
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        bool isnull;
        Datum datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
        
        if (!isnull) {
            bytea *binary_bytea = DatumGetByteaP(datum);
            size_t data_size = VARSIZE(binary_bytea) - VARHDRSZ;
            
            // Use SPI_palloc to allocate struct in caller context
            result = (BinarySelectResult*) SPI_palloc(sizeof(BinarySelectResult));
            result->size = data_size;
            
            // Use SPI_palloc to allocate data memory in caller context
            result->data = SPI_palloc(data_size);
            memcpy(result->data, VARDATA(binary_bytea), data_size);
        }
    }
    
    // Clean up SPI resources
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
}

DualKeyBinarySelectResult* PostgreSQLUtils::executeBinarySelectByKey1(const char* table_name, int key1_value) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT key2, data FROM %s WHERE key1 = $1", table_name);
    
    // Prepare parameters
    Oid argtypes[1] = {INT4OID};
    Datum values[1];
    char nulls[1] = {' '};
    
    // Set key value parameter
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

DualKeyBinarySelectResult* PostgreSQLUtils::executeBinarySelectAllDualKey(const char* table_name) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement
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

// Dual key large object operations

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
    
    // Prepare parameters
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

LargeObjectSelectResult* PostgreSQLUtils::executeLargeObjectSelectByDualKey(const char* table_name, int key1_value, int key2_value) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT lo_oid FROM %s WHERE key1 = $1 AND key2 = $2", table_name);
    
    // Prepare parameters
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

DualKeyBinarySelectResult* PostgreSQLUtils::executeLargeObjectSelectByKey1(const char* table_name, int key1_value) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement
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

DualKeyBinarySelectResult* PostgreSQLUtils::executeLargeObjectSelectAllDualKey(const char* table_name) {
    std::lock_guard<std::mutex> lock(spi_mutex_);
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement
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