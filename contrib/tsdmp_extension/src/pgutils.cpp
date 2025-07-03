#include "../include/safe_header.h"
#include "../include/pgutils.h"

// PostgreSQL数据库工具函数实现

void execute_sql(const char* sql)
{
    elog(INFO, "execute_sql: %s", sql);

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

SPITupleTable* execute_sql_select(const char* sql)
{
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
        // 使用SPI_palloc在调用者上下文中分配SPITupleTable
        result = (SPITupleTable*) SPI_palloc(sizeof(SPITupleTable));
        
        // 复制基本信息
        result->alloced = SPI_processed;
        // result->free = 0;  // 'free' member removed in newer PostgreSQL versions
        
        // 复制TupleDesc到调用者上下文
        result->tupdesc = CreateTupleDescCopy(SPI_tuptable->tupdesc);
        
        // 分配HeapTuple数组
        result->vals = (HeapTuple*) SPI_palloc(SPI_processed * sizeof(HeapTuple));
        
        // 复制每个HeapTuple到调用者上下文
        for (uint64 i = 0; i < SPI_processed; i++) {
            result->vals[i] = SPI_copytuple(SPI_tuptable->vals[i]);
        }
    }
    
    // 完成SPI操作
    SPI_finish();
    
    return result;
}

void execute_binary_insert(const char* table_name, int key_value, 
                          const void* binary_data, size_t binary_size)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // 构建INSERT语句
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "INSERT INTO %s (key, data) VALUES ($1, $2)", table_name);
    
    // 准备参数
    Oid argtypes[2] = {INT4OID, BYTEAOID};
    Datum values[2];
    char nulls[2] = {' ', ' '};
    
    // 设置键值参数
    values[0] = Int32GetDatum(key_value);
    
    // 设置二进制数据参数
    bytea *binary_bytea = (bytea *) palloc(VARHDRSZ + binary_size);
    SET_VARSIZE(binary_bytea, VARHDRSZ + binary_size);
    memcpy(VARDATA(binary_bytea), binary_data, binary_size);
    values[1] = PointerGetDatum(binary_bytea);
    
    // 准备并执行计划
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
    
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
}

BinarySelectResult* execute_binary_select(const char* table_name, int key_value)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // 构建SELECT语句
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT data FROM %s WHERE key = $1", table_name);
    
    // 准备参数
    Oid argtypes[1] = {INT4OID};
    Datum values[1];
    char nulls[1] = {' '};
    
    // 设置键值参数
    values[0] = Int32GetDatum(key_value);
    
    // 准备并执行计划
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
        // 获取结果
        HeapTuple tuple = SPI_tuptable->vals[0];
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        bool isnull;
        Datum datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
        
        if (!isnull) {
            bytea *binary_bytea = DatumGetByteaP(datum);
            size_t data_size = VARSIZE(binary_bytea) - VARHDRSZ;
            
            // 使用SPI_palloc在调用者上下文中分配结构体
            result = (BinarySelectResult*) SPI_palloc(sizeof(BinarySelectResult));
            result->size = data_size;
            
            // 使用SPI_palloc在调用者上下文中分配数据内存
            result->data = SPI_palloc(data_size);
            memcpy(result->data, VARDATA(binary_bytea), data_size);
        }
    }
    
    // 清理SPI资源
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
}

BinarySelectAllResult* execute_binary_select_all(const char* table_name)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // 构建SELECT语句
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
        // 使用SPI_palloc在调用者上下文中分配结构体
        result = (BinarySelectAllResult*) SPI_palloc(sizeof(BinarySelectAllResult));
        result->count = SPI_processed;
        
        // 使用SPI_palloc分配主数组
        result->keys = (int*) SPI_palloc(result->count * sizeof(int));
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            
            // 获取键值
            bool isnull;
            Datum key_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            result->keys[i] = DatumGetInt32(key_datum);
            
            // 获取二进制数据
            Datum data_datum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
            if (!isnull) {
                bytea *binary_bytea = DatumGetByteaP(data_datum);
                result->size_array[i] = VARSIZE(binary_bytea) - VARHDRSZ;
                
                // 使用SPI_palloc分配数据内存
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

void execute_largeobject_clear_table(const char* table_name)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // 先查询所有大对象OID
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT lo_oid FROM %s", table_name);
    
    int ret = SPI_exec(sql_buf.data, 0);
    if (ret == SPI_OK_SELECT) {
        // 删除所有大对象
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
    
    // 清空表
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


void execute_largeobject_insert(const char* table_name, int key_value,
                               const void* binary_data, size_t binary_size)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // 创建大对象
    Oid lo_oid = inv_create(INV_READ | INV_WRITE);
    if (lo_oid == InvalidOid) {
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("lo_creat failed")));
    }
    
    // 打开大对象
    LargeObjectDesc *lobj_desc = inv_open(lo_oid, INV_WRITE, CurrentMemoryContext);
    if (lobj_desc == NULL) {
        inv_drop(lo_oid);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("lo_open failed")));
    }
    
    // 写入数据
    int nbytes = inv_write(lobj_desc, (char*)binary_data, binary_size);
    if (nbytes != binary_size) {
        inv_close(lobj_desc);
        inv_drop(lo_oid);
        SPI_finish();
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("lo_write failed")));
    }
    
    // 关闭大对象
    inv_close(lobj_desc);
    
    // 插入记录
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "INSERT INTO %s (key, lo_oid) VALUES ($1, $2)", table_name);
    
    // 准备参数
    Oid argtypes[2] = {INT4OID, OIDOID};
    Datum values[2];
    char nulls[2] = {' ', ' '};
    
    values[0] = Int32GetDatum(key_value);
    values[1] = ObjectIdGetDatum(lo_oid);
    
    // 准备并执行计划
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

LargeObjectSelectResult* execute_largeobject_select_by_key(const char* table_name, int key_value)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // 构建SELECT语句
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT lo_oid FROM %s WHERE key = $1", table_name);
    
    // 准备参数
    Oid argtypes[1] = {INT4OID};
    Datum values[1];
    char nulls[1] = {' '};
    
    // 设置键值参数
    values[0] = Int32GetDatum(key_value);
    
    // 准备并执行计划
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
        // 使用SPI_palloc分配结果结构体
        result = (LargeObjectSelectResult*) SPI_palloc(sizeof(LargeObjectSelectResult));
        result->count = SPI_processed;
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        // 读取每个大对象的数据
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            bool isnull;
            Datum datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc, 1, &isnull);
            
            if (!isnull) {
                Oid lo_oid = DatumGetObjectId(datum);
                
                // 打开大对象
                LargeObjectDesc *lobj_desc = inv_open(lo_oid, INV_READ, CurrentMemoryContext);
                if (lobj_desc == NULL) {
                    SPI_freeplan(plan);
                    pfree(sql_buf.data);
                    SPI_finish();
                    ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                                   errmsg("lo_open failed for OID %u", lo_oid)));
                }
                
                // 获取大对象大小
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
                
                // 分配内存并读取数据
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

void execute_binary_insert_dual_key(const char* table_name, int key1_value, int key2_value,
                                   const void* binary_data, size_t binary_size)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // 构建INSERT语句
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "INSERT INTO %s (key1, key2, data) VALUES ($1, $2, $3)", table_name);
    
    // 准备参数
    Oid argtypes[3] = {INT4OID, INT4OID, BYTEAOID};
    Datum values[3];
    char nulls[3] = {' ', ' ', ' '};
    
    // 设置键值参数
    values[0] = Int32GetDatum(key1_value);
    values[1] = Int32GetDatum(key2_value);
    
    // 设置二进制数据参数
    bytea *binary_bytea = (bytea *) palloc(VARHDRSZ + binary_size);
    SET_VARSIZE(binary_bytea, VARHDRSZ + binary_size);
    memcpy(VARDATA(binary_bytea), binary_data, binary_size);
    values[2] = PointerGetDatum(binary_bytea);
    
    // 准备并执行计划
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

void execute_binary_upsert_dual_key(const char* table_name, int key1_value, int key2_value,
                                   const void* binary_data, size_t binary_size)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // 构建UPSERT语句 (INSERT ... ON CONFLICT ... DO UPDATE)
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, 
        "INSERT INTO %s (key1, key2, data) VALUES ($1, $2, $3) "
        "ON CONFLICT (key1, key2) DO UPDATE SET data = $3", table_name);
    
    // 准备参数
    Oid argtypes[3] = {INT4OID, INT4OID, BYTEAOID};
    Datum values[3];
    char nulls[3] = {' ', ' ', ' '};
    
    // 设置键值参数
    values[0] = Int32GetDatum(key1_value);
    values[1] = Int32GetDatum(key2_value);
    
    // 设置二进制数据参数
    bytea *binary_bytea = (bytea *) palloc(VARHDRSZ + binary_size);
    SET_VARSIZE(binary_bytea, VARHDRSZ + binary_size);
    memcpy(VARDATA(binary_bytea), binary_data, binary_size);
    values[2] = PointerGetDatum(binary_bytea);
    
    // 准备并执行计划
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

BinarySelectResult* execute_binary_select_by_dual_key(const char* table_name, int key1_value, int key2_value)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // 构建SELECT语句
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT data FROM %s WHERE key1 = $1 AND key2 = $2", table_name);
    
    // 准备参数
    Oid argtypes[2] = {INT4OID, INT4OID};
    Datum values[2];
    char nulls[2] = {' ', ' '};
    
    // 设置键值参数
    values[0] = Int32GetDatum(key1_value);
    values[1] = Int32GetDatum(key2_value);
    
    // 准备并执行计划
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
        // 获取结果
        HeapTuple tuple = SPI_tuptable->vals[0];
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        bool isnull;
        Datum datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
        
        if (!isnull) {
            bytea *binary_bytea = DatumGetByteaP(datum);
            size_t data_size = VARSIZE(binary_bytea) - VARHDRSZ;
            
            // 使用SPI_palloc在调用者上下文中分配结构体
            result = (BinarySelectResult*) SPI_palloc(sizeof(BinarySelectResult));
            result->size = data_size;
            
            // 使用SPI_palloc在调用者上下文中分配数据内存
            result->data = SPI_palloc(data_size);
            memcpy(result->data, VARDATA(binary_bytea), data_size);
        }
    }
    
    // 清理SPI资源
    SPI_freeplan(plan);
    pfree(sql_buf.data);
    SPI_finish();
    
    return result;
}

DualKeyBinarySelectResult* execute_binary_select_by_key1(const char* table_name, int key1_value)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // 构建SELECT语句
    StringInfoData sql_buf;
    initStringInfo(&sql_buf);
    appendStringInfo(&sql_buf, "SELECT key2, data FROM %s WHERE key1 = $1", table_name);
    
    // 准备参数
    Oid argtypes[1] = {INT4OID};
    Datum values[1];
    char nulls[1] = {' '};
    
    // 设置键值参数
    values[0] = Int32GetDatum(key1_value);
    
    // 准备并执行计划
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
        // 使用SPI_palloc在调用者上下文中分配结构体
        result = (DualKeyBinarySelectResult*) SPI_palloc(sizeof(DualKeyBinarySelectResult));
        result->count = SPI_processed;
        
        // 使用SPI_palloc分配数组
        result->key1_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->key2_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            
            // 获取key2值
            bool isnull;
            Datum key2_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            result->key1_array[i] = key1_value;  // key1都是相同的
            result->key2_array[i] = DatumGetInt32(key2_datum);
            
            // 获取二进制数据
            Datum data_datum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
            if (!isnull) {
                bytea *binary_bytea = DatumGetByteaP(data_datum);
                result->size_array[i] = VARSIZE(binary_bytea) - VARHDRSZ;
                
                // 使用SPI_palloc分配数据内存
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

DualKeyBinarySelectResult* execute_binary_select_all_dual_key(const char* table_name)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("could not connect to SPI")));
    }
    
    // 构建SELECT语句
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
        // 使用SPI_palloc在调用者上下文中分配结构体
        result = (DualKeyBinarySelectResult*) SPI_palloc(sizeof(DualKeyBinarySelectResult));
        result->count = SPI_processed;
        
        // 使用SPI_palloc分配数组
        result->key1_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->key2_array = (int*) SPI_palloc(result->count * sizeof(int));
        result->data_array = (void**) SPI_palloc(result->count * sizeof(void*));
        result->size_array = (size_t*) SPI_palloc(result->count * sizeof(size_t));
        
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        
        for (int i = 0; i < result->count; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];
            
            // 获取key1和key2值
            bool isnull;
            Datum key1_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            result->key1_array[i] = DatumGetInt32(key1_datum);
            
            Datum key2_datum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
            result->key2_array[i] = DatumGetInt32(key2_datum);
            
            // 获取二进制数据
            Datum data_datum = SPI_getbinval(tuple, tupdesc, 3, &isnull);
            if (!isnull) {
                bytea *binary_bytea = DatumGetByteaP(data_datum);
                result->size_array[i] = VARSIZE(binary_bytea) - VARHDRSZ;
                
                // 使用SPI_palloc分配数据内存
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