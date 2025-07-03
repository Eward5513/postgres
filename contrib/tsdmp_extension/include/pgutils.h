#ifndef PGUTILS_H
#define PGUTILS_H

#include "safe_header.h"

// PostgreSQL数据库工具函数
// 这些函数提供了便捷的SPI接口用于执行SQL语句

/**
 * 二进制查询结果结构体
 */
typedef struct BinarySelectResult {
    void* data;      // 二进制数据指针
    size_t size;     // 二进制数据大小
} BinarySelectResult;

/**
 * 批量二进制查询结果结构体
 */
typedef struct BinarySelectAllResult {
    int count;           // 结果数量
    int* keys;           // 键值数组
    void** data_array;   // 二进制数据指针数组
    size_t* size_array;  // 二进制数据大小数组
} BinarySelectAllResult;

/**
 * 大对象查询结果结构体
 */
typedef struct LargeObjectSelectResult {
    int count;           // 结果数量
    void** data_array;   // 二进制数据指针数组
    size_t* size_array;  // 二进制数据大小数组
} LargeObjectSelectResult;

/**
 * 执行不返回结果的SQL语句（如INSERT, UPDATE, DELETE, CREATE等）
 * @param sql 要执行的SQL语句
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 */
void execute_sql(const char* sql);

/**
 * 执行返回结果的SQL查询语句（如SELECT）
 * @param sql 要执行的SQL查询语句
 * @return SPITupleTable* 查询结果，结果已复制到调用者上下文中
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 * 
 * 注意：函数内部已处理SPI_finish()，调用者直接使用返回的结果即可
 */
SPITupleTable* execute_sql_select(const char* sql);

/**
 * 执行带二进制参数的INSERT语句
 * @param table_name 表名
 * @param key_value 键值
 * @param binary_data 二进制数据指针
 * @param binary_size 二进制数据大小
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 */
void execute_binary_insert(const char* table_name, int key_value, 
                          const void* binary_data, size_t binary_size);

/**
 * 执行带参数的SELECT语句查询二进制数据
 * @param table_name 表名
 * @param key_value 键值
 * @return BinarySelectResult* 查询结果，如果没有找到返回NULL
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 * 
 * 注意：函数内部已处理SPI_finish()，结果已复制到调用者上下文中，调用者直接使用返回的结果即可
 */
BinarySelectResult* execute_binary_select(const char* table_name, int key_value);

/**
 * 执行SELECT语句查询所有二进制数据
 * @param table_name 表名
 * @return BinarySelectAllResult* 查询结果，如果没有找到返回NULL
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 * 
 * 注意：函数内部已处理SPI_finish()，结果已复制到调用者上下文中，调用者直接使用返回的结果即可
 */
BinarySelectAllResult* execute_binary_select_all(const char* table_name);

/**
 * 双键二进制查询结果结构体
 */
typedef struct DualKeyBinarySelectResult {
    int count;              // 结果数量
    int* key1_array;        // key1值数组
    int* key2_array;        // key2值数组  
    void** data_array;      // 二进制数据指针数组
    size_t* size_array;     // 二进制数据大小数组
} DualKeyBinarySelectResult;

/**
 * 执行带双键的二进制数据插入操作
 * @param table_name 表名
 * @param key1_value 第一个键值
 * @param key2_value 第二个键值
 * @param binary_data 二进制数据指针
 * @param binary_size 二进制数据大小
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 */
void execute_binary_insert_dual_key(const char* table_name, int key1_value, int key2_value,
                                   const void* binary_data, size_t binary_size);

/**
 * 执行带双键的二进制数据插入或更新操作(UPSERT)
 * @param table_name 表名
 * @param key1_value 第一个键值
 * @param key2_value 第二个键值
 * @param binary_data 二进制数据指针
 * @param binary_size 二进制数据大小
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 */
void execute_binary_upsert_dual_key(const char* table_name, int key1_value, int key2_value,
                                   const void* binary_data, size_t binary_size);

/**
 * 根据双键查询二进制数据
 * @param table_name 表名
 * @param key1_value 第一个键值
 * @param key2_value 第二个键值
 * @return BinarySelectResult* 查询结果，如果没有找到返回NULL
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 */
BinarySelectResult* execute_binary_select_by_dual_key(const char* table_name, int key1_value, int key2_value);

/**
 * 根据key1查询所有相关的二进制数据
 * @param table_name 表名
 * @param key1_value 第一个键值
 * @return DualKeyBinarySelectResult* 查询结果，如果没有找到返回NULL
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 */
DualKeyBinarySelectResult* execute_binary_select_by_key1(const char* table_name, int key1_value);

/**
 * 查询所有双键二进制数据
 * @param table_name 表名
 * @return DualKeyBinarySelectResult* 查询结果，如果没有找到返回NULL
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 */
DualKeyBinarySelectResult* execute_binary_select_all_dual_key(const char* table_name);

/**
 * 清空大对象表并重新创建
 * @param table_name 表名
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 */
void execute_largeobject_clear_table(const char* table_name);

/**
 * 执行大对象插入操作
 * @param table_name 表名
 * @param key_value 键值
 * @param binary_data 二进制数据指针
 * @param binary_size 二进制数据大小
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 */
void execute_largeobject_insert(const char* table_name, int key_value,
                               const void* binary_data, size_t binary_size);

/**
 * 查询指定key的所有大对象数据
 * @param table_name 表名
 * @param key_value 键值
 * @return LargeObjectSelectResult* 查询结果，如果没有找到返回NULL
 * @throws 如果执行失败会抛出PostgreSQL ERROR
 * 
 * 注意：函数内部已处理SPI_finish()，结果已复制到调用者上下文中，调用者直接使用返回的结果即可
 */
LargeObjectSelectResult* execute_largeobject_select_by_key(const char* table_name, int key_value);

#endif // PGUTILS_H 