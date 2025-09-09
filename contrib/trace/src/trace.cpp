// 使用统一的安全头文件处理PostgreSQL和libintl.h冲突
#include "../include/safe_header.h"

// C++标准库头文件
#include <cstdarg>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

// 第三方库
#include "nlohmann/json.hpp"

// 项目头文件
#include "../include/trace.h"
#include "../include/parameter.h"
#include "../include/data_loader.h"
#include "../include/pgutils.h"

namespace fs = std::filesystem;

using std::vector;
using std::string;
using json = nlohmann::json;
using std::ofstream;
using std::ifstream;

SimpleBounds global_bounds;
std::vector<std::string> data_source_files;
std::map<std::string, std::string> config_map;

namespace trace {

// Configuration management
void load_config()
{
    // TODO: Implement configuration loading
    config_map["version"] = "1.0.0";
    config_map["status"] = "loaded";
}

void save_config()
{
    // TODO: Implement configuration saving
}

std::string get_config_value(const std::string& key)
{
    auto it = config_map.find(key);
    if (it != config_map.end()) {
        return it->second;
    }
    return "";
}

void set_config_value(const std::string& key, const std::string& value)
{
    config_map[key] = value;
}

// Type conversion utilities
Oid get_type_oid(const char* type_name)
{
    // TODO: Implement type OID lookup
    return InvalidOid;
}

TupleDesc trace_TypeGetTupleDesc(Oid type_oid, List* coldeflist)
{
    // TODO: Implement tuple descriptor creation
    return NULL;
}

// Error handling
void trace_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    
    va_end(args);
    
    ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR), errmsg("%s", buffer)));
}

void trace_warning(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    
    va_end(args);
    
    ereport(WARNING, (errmsg("%s", buffer)));
}

void trace_info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    
    va_end(args);
    
    ereport(INFO, (errmsg("%s", buffer)));
}

// Type conversion functions
SimpleBounds bounds_from_pg_args(float min_x, float min_y, float min_z, 
                                float max_x, float max_y, float max_z,
                                float min_time, float max_time)
{
    return SimpleBounds{};
}

SimplePoint point_from_pg_args(float x, float y, float z, float time)
{
    return SimplePoint{};
}

// PostgreSQL result creation functions
HeapTuple create_load_result_tuple(const LoadResult& result, TupleDesc tupdesc)
{
    Datum values[14];  // 增加到14个字段 (0-13)
    bool nulls[14] = {false};  // 初始化所有为 false
    
    values[0] = Int32GetDatum(result.files_loaded);
    values[1] = Int64GetDatum(result.total_points);
    values[2] = Float4GetDatum(result.load_time_seconds);
    
    // 边界信息
    values[3] = Float4GetDatum(result.min_x);
    values[4] = Float4GetDatum(result.max_x);
    values[5] = Float4GetDatum(result.min_y);
    values[6] = Float4GetDatum(result.max_y);
    values[7] = Float4GetDatum(result.min_z);
    values[8] = Float4GetDatum(result.max_z);
    values[9] = Float4GetDatum(result.min_time);
    values[10] = Float4GetDatum(result.max_time);
    
    // 统计信息
    values[11] = Int64GetDatum(result.total_file_size_bytes);
    values[12] = Float4GetDatum(result.avg_points_per_file);
    values[13] = CStringGetTextDatum(result.dataset_path.c_str());
    
    return heap_form_tuple(tupdesc, values, nulls);
}

HeapTuple create_index_result_tuple(const IndexResult& result, TupleDesc tupdesc)
{
    Datum values[4];
    bool nulls[4] = {false, false, false, false};
    
    values[0] = Int32GetDatum(result.total_octree_nodes);
    values[1] = Float4GetDatum(result.index_build_time);
    values[2] = Float4GetDatum(0.0); // index size
    values[3] = CStringGetTextDatum("Success");
    
    return heap_form_tuple(tupdesc, values, nulls);
}

HeapTuple create_spatiotemporal_point_tuple(const SimplePoint& point, TupleDesc tupdesc)
{
    Datum values[8];
    bool nulls[8] = {false, false, false, false, false, false, false, false};
    
    values[0] = Float4GetDatum(point.x);
    values[1] = Float4GetDatum(point.y);
    values[2] = Float4GetDatum(point.z);
    values[3] = Float4GetDatum(point.time);
    values[4] = Int32GetDatum(point.data_type);
    values[5] = Int32GetDatum(point.fid);
    values[6] = Int32GetDatum(point.pid);
    values[7] = Int32GetDatum(point.foreign_key);
    
    return heap_form_tuple(tupdesc, values, nulls);
}

HeapTuple create_knn_result_tuple(const KnnResult& result, TupleDesc tupdesc)
{
    Datum values[9];
    bool nulls[9] = {false, false, false, false, false, false, false, false, false};
    
    values[0] = Float4GetDatum(result.distance);
    values[1] = Float4GetDatum(result.point.x);
    values[2] = Float4GetDatum(result.point.y);
    values[3] = Float4GetDatum(result.point.z);
    values[4] = Float4GetDatum(result.point.time);
    values[5] = Int32GetDatum(result.point.data_type);
    values[6] = Int32GetDatum(result.point.fid);
    values[7] = Int32GetDatum(result.point.pid);
    values[8] = Int32GetDatum(result.point.foreign_key);
    
    return heap_form_tuple(tupdesc, values, nulls);
}

} // namespace trace

// Main implementation functions called by extension.cpp

// Configuration initialization
void initialize_config()
{
    config_map["version"] = "1.0.0";
    config_map["status"] = "initialized";
    config_map["build_time"] = __DATE__ " " __TIME__;
    
    elog(INFO, "TSDMP configuration initialized");
}

// Data loading implementation
LoadResult trace_load_data_impl(const std::string& directory, 
                               int max_file_num, float sample_ratio)
{
    LoadResult result{};
    result.dataset_path = directory;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Check if directory exists and is not empty
    fs::path dir_path(directory);
    if (!fs::exists(dir_path)) {
        elog(ERROR, "Directory '%s' does not exist", directory.c_str());
    }
    
    if (!fs::is_directory(dir_path)) {
        elog(ERROR, "Path '%s' is not a directory", directory.c_str());
    }
    
    if (fs::is_empty(dir_path)) {
        elog(ERROR, "Directory '%s' is empty", directory.c_str());
    }

    // 直接遍历文件夹获取文件列表
    std::vector<std::string> filenames;
    for (const auto &entry : fs::directory_iterator(directory))
    {
        const auto &path = entry.path();
        auto filename = path.filename().string();
        
        // Skip hidden files and directories
        if (filename[0] == '.' || entry.is_directory()) {
            continue;
        }
        
        // Only accept CSV files (comma-separated values)
        if (path.extension() == ".csv") {
            filenames.push_back(path.string());
        }
    }
    
    // Apply file limit if specified
    if (max_file_num > 0 && filenames.size() > max_file_num) {
        filenames.resize(max_file_num);
    }
    
    elog(INFO, "Found %zu data files to process", filenames.size());
    
    // 创建 DataLoader 实例并传入文件名数组处理，直接获取计算的边界
    DataLoader dataLoader(directory, max_file_num, sample_ratio);
    SimpleBounds bounds = dataLoader.load_data(filenames);  // 传入文件名数组并获取边界

    // 收集详细统计信息
    result.files_loaded = filenames.size();
    result.total_file_size_bytes = 0;
    result.loaded_file_paths = filenames;  // 存储文件路径到 LoadResult 中
    
    // 计算文件大小
    for (const auto& filename : filenames) {
        fs::path file_path(filename);
        if (fs::exists(file_path)) {
            result.total_file_size_bytes += fs::file_size(file_path);
        }
    }
    // 检查是否有有效的边界数据（非零值表示有效数据）
    bool has_valid_bounds = (bounds.min_x != 0.0f || bounds.max_x != 0.0f || 
                            bounds.min_y != 0.0f || bounds.max_y != 0.0f ||
                            bounds.min_z != 0.0f || bounds.max_z != 0.0f);
    
    if (has_valid_bounds) {
        // 有效的边界数据
        result.min_x = bounds.min_x;
        result.max_x = bounds.max_x;
        result.min_y = bounds.min_y;
        result.max_y = bounds.max_y;
        result.min_z = bounds.min_z;
        result.max_z = bounds.max_z;
        result.min_time = bounds.min_time;
        result.max_time = bounds.max_time;
        
        // 估算点数（可以基于文件数量和边界范围进行更精确的估算）
        result.total_points = filenames.size() * 1000; // 临时估算，可以改进
    } else {
        // 没有有效数据，使用默认值
        elog(WARNING, "No valid boundary data found, using default values");
        result.min_x = result.min_y = result.min_z = result.min_time = 0.0f;
        result.max_x = result.max_y = result.max_z = 100.0f;
        result.max_time = 1000.0f;
        result.total_points = 0;
    }
    
    // 计算平均值
    result.avg_points_per_file = result.files_loaded > 0 ? 
        (float)result.total_points / result.files_loaded : 0.0f;
    
    // 计算加载时间
    auto end_time = std::chrono::high_resolution_clock::now();
    result.load_time_seconds = std::chrono::duration<float>(end_time - start_time).count();
    
    // 存储到全局变量
    data_source_files = filenames;
    // 设置全局边界（使用计算出的边界）
    global_bounds = SimpleBounds(result.min_x, result.min_y, result.min_z, result.min_time,
                                result.max_x, result.max_y, result.max_z, result.max_time);
    
    elog(INFO, "Loaded %d files from directory '%s' with %ld total points, "
              "bounds: Longitude[%.6f-%.6f], Latitude[%.6f-%.6f], Altitude[%.3f-%.3f], "
              "total size: %ld bytes, avg points/file: %.1f", 
         result.files_loaded, directory.c_str(), result.total_points,
         result.min_x, result.max_x, result.min_y, result.max_y, 
         result.min_z, result.max_z,
         result.total_file_size_bytes, result.avg_points_per_file);
    
    return result;
}

// Index building implementation
IndexResult trace_build_index_impl(int chunk_max_level_param, int octree_max_level_param, int max_point_per_leaf_param)
{
    IndexResult result{};
    
    // Update global parameters with user-specified values
    ::chunk_max_level = chunk_max_level_param;
    ::octree_max_level = octree_max_level_param;
    ::max_point_per_leaf = max_point_per_leaf_param;
    
    // Store parameters in config for logging/debugging
    config_map["chunk_max_level"] = std::to_string(chunk_max_level_param);
    config_map["octree_max_level"] = std::to_string(octree_max_level_param);
    config_map["max_point_per_leaf"] = std::to_string(max_point_per_leaf_param);
    
    // Simulate index building
    result.index_build_time = 2.5; // Dummy time
    result.chunk_count = data_source_files.size();
    result.total_octree_nodes = 1000; // Dummy count
    result.total_kdtree_nodes = 500; // Dummy count
    
    elog(INFO, "Built index with %d chunks, %d octree nodes, %d kdtree nodes in %.2f seconds",
         result.chunk_count, result.total_octree_nodes, 
         result.total_kdtree_nodes, result.index_build_time);
    
    return result;
}

// Range query implementation
std::vector<SimplePoint> trace_range_query_impl(const SimpleBounds& bounds, int data_type_mask)
{
    std::vector<SimplePoint> results;
    
    // TODO: Implement actual range query logic
    // For now, just return some dummy points within bounds
    
    elog(DEBUG1, "Performing range query: bounds(%.2f,%.2f,%.2f,%.2f) to (%.2f,%.2f,%.2f,%.2f), type_mask=%d",
         bounds.min_x, bounds.min_y, bounds.min_z, bounds.min_time,
         bounds.max_x, bounds.max_y, bounds.max_z, bounds.max_time, data_type_mask);
    
    // Generate some dummy results
    for (int i = 0; i < 5; i++) {
        SimplePoint point;
        point.x = bounds.min_x + (bounds.max_x - bounds.min_x) * 0.5f;
        point.y = bounds.min_y + (bounds.max_y - bounds.min_y) * 0.5f;
        point.z = bounds.min_z + (bounds.max_z - bounds.min_z) * 0.5f;
        point.time = bounds.min_time + (bounds.max_time - bounds.min_time) * 0.5f;
        point.data_type = data_type_mask & TRACE_TYPE_POINTCLOUD;
        point.fid = i;
        point.pid = i * 100;
        point.foreign_key = i;
        
        results.push_back(point);
    }
    
    return results;
}

// kNN query implementation
std::vector<std::pair<SimplePoint, float>> trace_knn_query_impl(const SimplePoint& center, int k, 
                                                                float min_time, float max_time, 
                                                                int data_type_mask)
{
    std::vector<std::pair<SimplePoint, float>> results;
    
    // TODO: Implement actual kNN query logic
    // For now, just return some dummy nearest neighbors
    
    elog(DEBUG1, "Performing kNN query: center(%.2f,%.2f,%.2f,%.2f), k=%d, time(%.2f,%.2f), type_mask=%d",
         center.x, center.y, center.z, center.time, k, min_time, max_time, data_type_mask);
    
    // Generate k dummy results at increasing distances
    for (int i = 0; i < k && i < 10; i++) {
        SimplePoint point;
        point.x = center.x + i * 0.1f;
        point.y = center.y + i * 0.1f;
        point.z = center.z + i * 0.1f;
        point.time = (min_time + max_time) * 0.5f;
        point.data_type = data_type_mask & TRACE_TYPE_POINTCLOUD;
        point.fid = i;
        point.pid = i * 100;
        point.foreign_key = i;
        
        float distance = i * 0.1f * sqrt(3); // Distance from center
        
        results.push_back(std::make_pair(point, distance));
    }
    
    return results;
}

// Configuration management implementations
void trace_set_config_impl(const std::string& key, const std::string& value)
{
    // config_map[key] = value;
    elog(DEBUG1, "Config set: %s = %s", key.c_str(), value.c_str());
}

std::string trace_get_config_impl(const std::string& key)
{
    auto it = config_map.find(key);
    if (it != config_map.end()) {
        return it->second;
    }
    return "";
}

// Data clearing implementation
bool trace_clear_data_impl()
{
    // Clear all data structures
    data_source_files.clear();
    global_bounds = SimpleBounds();
    
    // Clear some config but keep version info
    std::string version = config_map["version"];
    config_map.clear();
    config_map["version"] = version;
    config_map["status"] = "cleared";
    
    elog(INFO, "All data structures cleared");
    return true;
}

// Statistics implementation
StatsResult trace_get_stats_impl()
{
    StatsResult stats;
    
    stats.total_files = data_source_files.size();
    stats.total_points = 0;
    stats.total_chunks = 0;
    stats.index_size_mb = 0.0f;
    
    // Calculate estimated points
    for (const auto& file : data_source_files) {
        stats.total_points += 100000; // Assume 100k points per file
    }
    
    // Get chunk count from config if available
    auto it = config_map.find("chunk_max_level");
    if (it != config_map.end()) {
        stats.total_chunks = std::stoi(it->second);
    }
    
    // Estimate index size
    stats.index_size_mb = stats.total_points * 0.000024f; // Rough estimate: 24 bytes per point
    
    elog(DEBUG1, "Stats: %d files, %ld points, %d chunks, %.2f MB",
         stats.total_files, stats.total_points, stats.total_chunks, stats.index_size_mb);
    
    return stats;
} 