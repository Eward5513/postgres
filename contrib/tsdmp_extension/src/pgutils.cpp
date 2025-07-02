#include "../include/safe_header.h"
#include "../include/pgutils.h"

// PostgreSQL数据库工具函数实现

void execute_sql(const char* sql)
{
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
    
    SPITupleTable* result = SPI_tuptable;
    // Note: caller is responsible for SPI_finish()
    return result;
} 