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
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // Build SELECT statement
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT data FROM %s WHERE key = $1", table_name);
    
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
    
    int ret = SPI_exec(sql_buf.data, 0);
    if (ret == SPI_OK_SELECT) {
        // Delete all large objects
        for (int i = 0; i < SPI_processed; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isnull;
            Datum datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc, 1, &isnull);
            if (!isnull) {
                Oid lo_oid = DatumGetObjectId(datum);
                inv_drop(lo_oid);
            }
        }
    }
    
    // Clear table
    pfree(sql_buf.data);
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "DELETE FROM %s", table_name);
    
    ret = SPI_exec(sql_buf.data, 0);
    if (ret != SPI_OK_DELETE) {
        pfree(sql_buf.data);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("SPI_exec failed for DELETE")));
    }
    
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