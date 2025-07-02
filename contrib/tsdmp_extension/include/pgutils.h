#ifndef PGUTILS_H
#define PGUTILS_H

#include "safe_header.h"

// PostgreSQL数据库工具函数
// 这些函数提供了便捷的SPI接口用于执行SQL语句

/**
 * 执行不返回结果的SQL语句（如INSERT, UPDATE, DELETE, CREATE等）
 * @param sql 要执行的SQL语句
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 */
void execute_sql(const char* sql);

/**
 * 执行返回结果的SQL查询语句（如SELECT）
 * @param sql 要执行的SQL查询语句
 * @return SPITupleTable* 查询结果，调用者负责调用SPI_finish()清理
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 * 
 * 注意：调用者必须在使用完结果后调用SPI_finish()来清理资源
 */
SPITupleTable* execute_sql_select(const char* sql);

#endif // PGUTILS_H 