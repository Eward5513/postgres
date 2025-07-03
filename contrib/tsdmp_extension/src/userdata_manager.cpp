#include "../include/safe_header.h"
#include "../include/userdata_manager.h"
#include "../include/pgutils.h"

void UserDataManager::clearTable() {
    // 清理表中的数据 (建表逻辑已移到SQL文件中)
    std::string sql = "TRUNCATE TABLE " + std::string(TABLE_NAME) + ";";
    execute_sql(sql.c_str());
}
// 一个key可能会对应多个大对象 是否需要待查
void UserDataManager::writeDataToDatabase(int key, const std::vector<SpatioTemporalData>& data) {
    // 使用SPI接口插入大对象数据
    execute_largeobject_insert(TABLE_NAME, key, data.data(), data.size() * sizeof(SpatioTemporalData));
}

std::vector<std::vector<SpatioTemporalData>> UserDataManager::loadDataFromDatabase(
    int key) {
    std::vector<std::vector<SpatioTemporalData>> data;

    // 使用SPI接口查询大对象数据
    LargeObjectSelectResult* result = execute_largeobject_select_by_key(TABLE_NAME, key);
    
    if (result == NULL) {
        return data; // 没有找到数据，返回空的data
    }
    
    // 预分配空间
    data.reserve(result->count);
    
    // 遍历所有大对象数据
    for (int i = 0; i < result->count; i++) {
        if (result->data_array[i] != NULL && result->size_array[i] > 0) {
            // 验证数据大小
            if (result->size_array[i] % sizeof(SpatioTemporalData) != 0) {
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                               errmsg("Invalid data size for SpatioTemporalData")));
            }
            
            // 添加数据到结果中
            data.emplace_back();
            size_t num_items = result->size_array[i] / sizeof(SpatioTemporalData);
            const SpatioTemporalData* buffer = reinterpret_cast<const SpatioTemporalData*>(result->data_array[i]);
            data.back().assign(buffer, buffer + num_items);
        }
    }
    
    // 释放内存
    for (int i = 0; i < result->count; i++) {
        if (result->data_array[i] != NULL) {
            pfree(result->data_array[i]);
        }
    }
    pfree(result->data_array);
    pfree(result->size_array);
    pfree(result);
    
    return data;
}