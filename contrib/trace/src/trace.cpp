// 使用统一的安全头文件处理PostgreSQL和libintl.h冲突
#include "../include/safe_header.h"

// C++标准库头文件
#include <cstdarg>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <queue>
#include <cstdint>
#include <limits>
#include <system_error>
#include <cstdlib>
#include <sstream>

// 第三方库
#include "nlohmann/json.hpp"

// 项目头文件
#include "../include/trace.h"
#include "../include/parameter.h"
#include "../include/data_loader.h"
#include "../include/pgutils.h"
#include "../include/index_storage.h"
#include "../include/index_builder.h"
#include "../include/morton_utils.h"

namespace fs = std::filesystem;

using std::vector;
using std::string;
using json = nlohmann::json;
using std::ofstream;
using std::ifstream;

// Global variables removed - data is now retrieved from database queries

// Function declarations
void suggest_guc_parameters(long long total_points);

namespace trace {

/**
 * @brief Get current dataset bounds from database
 * 
 * @param bounds SimpleBounds reference to store the retrieved bounds
 * @return true if bounds were successfully retrieved, false otherwise
 */
bool get_current_dataset_bounds(SimpleBounds& bounds) {
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(WARNING, "Failed to connect to SPI for reading dataset info");
        return false;
    }
    
    int ret = SPI_execute("SELECT min_x, max_x, min_y, max_y, min_z, max_z "
                         "FROM trace_dataset_info LIMIT 1", true, 1);
    
    if (ret == SPI_OK_SELECT && SPI_processed > 0) {
        HeapTuple tuple = SPI_tuptable->vals[0];
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        bool isnull;
        
        bounds.min_x = DatumGetFloat4(SPI_getbinval(tuple, tupdesc, 1, &isnull));
        bounds.max_x = DatumGetFloat4(SPI_getbinval(tuple, tupdesc, 2, &isnull));
        bounds.min_y = DatumGetFloat4(SPI_getbinval(tuple, tupdesc, 3, &isnull));
        bounds.max_y = DatumGetFloat4(SPI_getbinval(tuple, tupdesc, 4, &isnull));
        bounds.min_z = DatumGetFloat4(SPI_getbinval(tuple, tupdesc, 5, &isnull));
        bounds.max_z = DatumGetFloat4(SPI_getbinval(tuple, tupdesc, 6, &isnull));
        // Time fields removed from SimpleBounds structure
        
        SPI_finish();
        return true;
    }
    
    SPI_finish();
    return false;
}

/**
 * @brief Get dataset path and file list from database
 * 
 * @param dataset_path Reference to store the dataset path
 * @param file_paths Reference to vector to store file paths
 * @return true if data was successfully retrieved
 */
bool get_current_dataset_info(std::string& dataset_path, std::vector<std::string>& file_paths) {
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(WARNING, "Failed to connect to SPI for reading dataset info");
        return false;
    }
    
    int ret = SPI_execute("SELECT dataset_path, file_paths FROM trace_dataset_info LIMIT 1", true, 1);
    
    if (ret == SPI_OK_SELECT && SPI_processed > 0) {
        HeapTuple tuple = SPI_tuptable->vals[0];
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        bool isnull;
        
        // Get dataset path
        Datum datum_path = SPI_getbinval(tuple, tupdesc, 1, &isnull);
        if (!isnull) {
            char* path_cstr = TextDatumGetCString(datum_path);
            dataset_path = std::string(path_cstr);
        }
        
        // Get file paths (JSON array)
        Datum datum_files = SPI_getbinval(tuple, tupdesc, 2, &isnull);
        if (!isnull) {
            char* files_cstr = TextDatumGetCString(datum_files);
            std::string files_json(files_cstr);
            
            // Parse JSON array - simple implementation for CSV files
            // Remove brackets and split by comma
            if (files_json.length() > 2 && files_json.front() == '[' && files_json.back() == ']') {
                files_json = files_json.substr(1, files_json.length() - 2);
                std::stringstream ss(files_json);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    // Remove quotes and whitespace
                    item.erase(0, item.find_first_not_of(" \t\""));
                    item.erase(item.find_last_not_of(" \t\"") + 1);
                    if (!item.empty()) {
                        file_paths.push_back(item);
                    }
                }
            }
        }
        
        SPI_finish();
        return true;
    }
    
    SPI_finish();
    return false;
}

// Type conversion functions
SimpleBounds bounds_from_pg_args(float min_x, float min_y, float min_z, 
                                float max_x, float max_y, float max_z,
                                float min_time, float max_time)
{
    // Time parameters are ignored since SimpleBounds no longer contains time fields
    return SimpleBounds(min_x, min_y, min_z, max_x, max_y, max_z);
}

SimplePoint point_from_pg_args(float x, float y, float z, float time)
{
    SimplePoint point;
    point.x = x;
    point.y = y; 
    point.z = z;
    point.time = time;
    return point;
}

// PostgreSQL result creation functions
HeapTuple create_load_result_tuple(const LoadResult& result, TupleDesc tupdesc)
{
    Datum values[12];  // 减少到12个字段 (0-11) - 移除了时间字段
    bool nulls[12] = {false};  // 初始化所有为 false
    
    values[0] = Int32GetDatum(result.files_loaded);
    values[1] = Int64GetDatum(result.total_points);
    // 加载时间保持动态，但提高精度一致性
    values[2] = Float4GetDatum(result.load_time_seconds);
    
    // 边界信息 - 统一保持6位小数精度 (去掉时间边界)
    values[3] = Float4GetDatum(std::round(result.min_x * 1000000.0f) / 1000000.0f);
    values[4] = Float4GetDatum(std::round(result.max_x * 1000000.0f) / 1000000.0f);
    values[5] = Float4GetDatum(std::round(result.min_y * 1000000.0f) / 1000000.0f);
    values[6] = Float4GetDatum(std::round(result.max_y * 1000000.0f) / 1000000.0f);
    values[7] = Float4GetDatum(std::round(result.min_z * 1000000.0f) / 1000000.0f);
    values[8] = Float4GetDatum(std::round(result.max_z * 1000000.0f) / 1000000.0f);
    
    // 统计信息
    values[9] = Int64GetDatum(result.total_file_size_bytes);
    values[10] = Float4GetDatum(std::round(result.avg_points_per_file * 1000000.0f) / 1000000.0f);
    values[11] = CStringGetTextDatum(result.dataset_path.c_str());
    
    return heap_form_tuple(tupdesc, values, nulls);
}

HeapTuple create_index_result_tuple(const IndexResult& result, TupleDesc tupdesc)
{
    Datum values[4];
    bool nulls[4] = {false, false, false, false};
    
    values[0] = Float4GetDatum(result.index_build_time);
    values[1] = Int32GetDatum(result.chunk_count);
    values[2] = Int32GetDatum(result.total_octree_nodes);
    values[3] = Int32GetDatum(result.total_kdtree_nodes);
    
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
    // Return a flattened tuple with distance + point fields (without time_stamp)
    Datum values[8];
    bool nulls[8] = {false, false, false, false, false, false, false, false};
    
    values[0] = Float4GetDatum(result.distance);
    values[1] = Float4GetDatum(result.point.x);
    values[2] = Float4GetDatum(result.point.y);
    values[3] = Float4GetDatum(result.point.z);
    values[4] = Int16GetDatum((int16)result.point.data_type);
    values[5] = Int32GetDatum(result.point.fid);
    values[6] = Int32GetDatum(result.point.pid);
    values[7] = Int16GetDatum((int16)result.point.foreign_key); // user_id
    
    return heap_form_tuple(tupdesc, values, nulls);
}

} // namespace trace

// Main implementation functions called by extension.cpp

// Configuration initialization
void initialize_config()
{
    elog(DEBUG1, "TSDMP configuration initialized - version 1.0.0, build: %s %s", __DATE__, __TIME__);
}

// Helper function to store dataset information
static void store_dataset_info(const std::string& dataset_path, 
                              const LoadResult& result, 
                              float sample_ratio) {
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(WARNING, "Failed to connect to SPI for storing dataset info");
        return;
    }
    
    // Create JSON array for file paths from LoadResult
    std::string json_paths = "[";
    for (size_t i = 0; i < result.loaded_file_paths.size(); ++i) {
        if (i > 0) json_paths += ",";
        json_paths += "\"" + result.loaded_file_paths[i] + "\"";
    }
    json_paths += "]";
    
    // Escape single quotes in paths
    std::string escaped_dataset_path = dataset_path;
    std::string escaped_json_paths = json_paths;
    
    // Replace single quotes with two single quotes for SQL escaping
    size_t pos = 0;
    while ((pos = escaped_dataset_path.find("'", pos)) != std::string::npos) {
        escaped_dataset_path.replace(pos, 1, "''");
        pos += 2;
    }
    pos = 0;
    while ((pos = escaped_json_paths.find("'", pos)) != std::string::npos) {
        escaped_json_paths.replace(pos, 1, "''");
        pos += 2;
    }
    
    // First, try to delete all existing records to ensure single row
    char delete_query[256];
    snprintf(delete_query, sizeof(delete_query), "DELETE FROM trace_dataset_info");
    SPI_execute(delete_query, false, 0);
    
    // Then insert the new record (without time fields)
    char query[4096];
    snprintf(query, sizeof(query),
        "INSERT INTO trace_dataset_info "
        "(dataset_path, total_files, total_points, sample_ratio, "
        " min_x, max_x, min_y, max_y, min_z, max_z, "
        " load_duration_seconds, file_paths) "
        "VALUES ('%s', %d, %ld, %.3f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.3f, '%s')",
        escaped_dataset_path.c_str(), result.files_loaded, result.total_points, sample_ratio,
        result.min_x, result.max_x, result.min_y, result.max_y, 
        result.min_z, result.max_z,
        result.load_time_seconds, escaped_json_paths.c_str());
    
    int ret = SPI_execute(query, false, 0);
    if (ret != SPI_OK_INSERT && ret != SPI_OK_UPDATE) {
        elog(WARNING, "Failed to store dataset info: %d", ret);
    } else {
        elog(DEBUG1, "Dataset info stored: %s (%d files, %ld points, bounds: X[%.2f-%.2f], Y[%.2f-%.2f], Z[%.2f-%.2f])", 
             dataset_path.c_str(), result.files_loaded, result.total_points,
             result.min_x, result.max_x, result.min_y, result.max_y, result.min_z, result.max_z);
    }
    
    SPI_finish();
}

// Suggest optimal GUC parameters based on data size
void suggest_guc_parameters(long long total_points)
{
    int suggested_max_point_per_leaf, suggested_bucket_max_points, suggested_kd_leaf_max_points;
    
    if (total_points < 1000) {
        // Small dataset (< 1K points)
        suggested_max_point_per_leaf = 50;
        suggested_bucket_max_points = 8;
        suggested_kd_leaf_max_points = 4;
    } else if (total_points < 10000) {
        // Medium dataset (1K - 10K points)
        suggested_max_point_per_leaf = 200;
        suggested_bucket_max_points = 16;
        suggested_kd_leaf_max_points = 8;
    } else if (total_points < 100000) {
        // Large dataset (10K - 100K points)
        suggested_max_point_per_leaf = 1000;
        suggested_bucket_max_points = 64;
        suggested_kd_leaf_max_points = 16;
    } else if (total_points < 1000000) {
        // Very large dataset (100K - 1M points)
        suggested_max_point_per_leaf = 5000;
        suggested_bucket_max_points = 256;
        suggested_kd_leaf_max_points = 32;
    } else if (total_points < 10000000) {
        // Huge dataset (1M - 10M points)
        suggested_max_point_per_leaf = 10000;
        suggested_bucket_max_points = 512;
        suggested_kd_leaf_max_points = 32;
    } else {
        // Massive dataset (10M+ points)
        suggested_max_point_per_leaf = 20000;
        suggested_bucket_max_points = 1024;
        suggested_kd_leaf_max_points = 64;
    }
    
    // Calculate estimated index structure sizes
    int estimated_outer_leaves = (int)((total_points + suggested_max_point_per_leaf - 1) / suggested_max_point_per_leaf);
    int estimated_buckets = (int)((total_points + suggested_bucket_max_points - 1) / suggested_bucket_max_points);
    int estimated_kd_leaves = (int)((total_points + suggested_kd_leaf_max_points - 1) / suggested_kd_leaf_max_points);
    
    elog(NOTICE, "=== Suggested GUC Parameters for %lld data points ===", total_points);
    elog(NOTICE, "SET trace.max_point_per_leaf = %d;", suggested_max_point_per_leaf);
    elog(NOTICE, "SET trace.bucket_max_points = %d;", suggested_bucket_max_points);
    elog(NOTICE, "SET trace.kd_leaf_max_points = %d;", suggested_kd_leaf_max_points);
    elog(NOTICE, "Estimated index structure: %d outer leaves, %d buckets, %d kd leaves", 
         estimated_outer_leaves, estimated_buckets, estimated_kd_leaves);
    elog(NOTICE, "====================================================");
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
    
    elog(DEBUG1, "Found %zu data files to process", filenames.size());
    
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
        // Time fields removed from LoadResult
        
        // 获取实际点数（从DataLoader）
        result.total_points = dataLoader.getTotalPoints();
    } else {
        // 没有有效数据，使用默认值
        elog(WARNING, "No valid boundary data found, using default values");
        result.min_x = result.min_y = result.min_z = 0.0f;
        result.max_x = result.max_y = result.max_z = 100.0f;
        // Time fields removed
        result.total_points = 0;
    }
    
    // 计算平均值
    result.avg_points_per_file = result.files_loaded > 0 ? 
        (float)result.total_points / result.files_loaded : 0.0f;
    
    // 计算加载时间
    auto end_time = std::chrono::high_resolution_clock::now();
    result.load_time_seconds = std::chrono::duration<float>(end_time - start_time).count();
    
    // Global variables removed - data is now stored in database via store_dataset_info()
    
    elog(DEBUG1, "Loaded %d files from directory '%s' with %ld total points, "
              "bounds: Longitude[%.6f-%.6f], Latitude[%.6f-%.6f], Altitude[%.3f-%.3f], "
              "total size: %ld bytes, avg points/file: %.1f", 
         result.files_loaded, directory.c_str(), result.total_points,
         result.min_x, result.max_x, result.min_y, result.max_y, 
         result.min_z, result.max_z,
         result.total_file_size_bytes, result.avg_points_per_file);
    
    // Store dataset information in database
    store_dataset_info(directory, result, sample_ratio);
    
    // Suggest optimal GUC parameters based on data size
    suggest_guc_parameters(result.total_points);
    
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
    
    // Get dataset bounds and file list from database
    SimpleBounds bounds;
    std::string dataset_path;
    std::vector<std::string> data_source_files;
    
    if (!trace::get_current_dataset_bounds(bounds)) {
        elog(ERROR, "Failed to retrieve dataset bounds from database. Please load data first.");
        result.total_octree_nodes = 0;
        result.total_kdtree_nodes = 0;
        return result;
    }
    
    if (!trace::get_current_dataset_info(dataset_path, data_source_files)) {
        elog(ERROR, "Failed to retrieve dataset information from database. Please load data first.");
        result.total_octree_nodes = 0;
        result.total_kdtree_nodes = 0;
        return result;
    }
    
    if (data_source_files.empty()) {
        elog(ERROR, "No data files found in dataset. Please check data loading.");
        result.total_octree_nodes = 0;
        result.total_kdtree_nodes = 0;
        return result;
    }
    
    elog(DEBUG1, "Building index for dataset '%s' with %zu files, bounds: X[%.2f-%.2f], Y[%.2f-%.2f], Z[%.2f-%.2f]",
         dataset_path.c_str(), data_source_files.size(),
         bounds.min_x, bounds.max_x, bounds.min_y, bounds.max_y, bounds.min_z, bounds.max_z);
    
    // 使用 IndexBuilder 完成构建与持久化
    auto build_start = std::chrono::high_resolution_clock::now();
    IndexBuilder::Params p{ octree_max_level_param, max_point_per_leaf_param, trace_bucket_max_points, 6, trace_kd_leaf_max_points };
    IndexBuilder builder(dataset_path, bounds, data_source_files, p);
    builder.build_all(result);
    auto build_end = std::chrono::high_resolution_clock::now();
    result.index_build_time = std::chrono::duration<float>(build_end - build_start).count();

    return result;
}

// Range query implementation
std::vector<SimplePoint> trace_range_query_impl(const SimpleBounds& bounds, int data_type_mask)
{
    std::vector<SimplePoint> results;
    
    // Get dataset path from database
    std::string dataset_path;
    std::vector<std::string> file_paths; // Not used in query but needed for function
    
    if (!trace::get_current_dataset_info(dataset_path, file_paths)) {
        elog(WARNING, "Failed to retrieve dataset information for range query");
        return results;
    }
    
    // 使用桶（叶内 octree）粗筛，直接按 bucket 偏移读取
    auto buckets = db_query_buckets_intersecting(dataset_path, bounds);
    for (const auto &bmeta : buckets) {
        read_points_block_filter_bounds(bmeta.file_path, bmeta.offset, bmeta.count, bounds, data_type_mask, results);
    }
    
    return results;
}

// kNN query implementation
std::vector<std::pair<SimplePoint, float>> trace_knn_query_impl(const SimplePoint& center, int k, 
                                                                float min_time, float max_time, 
                                                                int data_type_mask)
{
    std::vector<std::pair<SimplePoint, float>> results;
    
    if (k <= 0) return results;
    if ((data_type_mask & TRACE_TYPE_POINTCLOUD) == 0) return results;

    // Get dataset path from database
    std::string dataset_path;
    std::vector<std::string> file_paths; // Not used in query but needed for function
    
    if (!trace::get_current_dataset_info(dataset_path, file_paths)) {
        elog(WARNING, "Failed to retrieve dataset information for kNN query");
        return results;
    }

    // 使用 kd 叶（桶内二分）的包围盒做粗序，从近到远读取块更新堆
    auto kdleaves = db_query_all_kdleaves(dataset_path);
    std::vector<std::pair<float,size_t>> order;
    order.reserve(kdleaves.size());
    for (size_t i=0;i<kdleaves.size();++i) {
        const auto &m = kdleaves[i];
        // 用 kd 叶 bbox 到点的最小距离
        float dx=0,dy=0,dz=0; if (center.x<m.minx) dx=m.minx-center.x; else if (center.x>m.maxx) dx=center.x-m.maxx;
        if (center.y<m.miny) dy=m.miny-center.y; else if (center.y>m.maxy) dy=center.y-m.maxy;
        if (center.z<m.minz) dz=m.minz-center.z; else if (center.z>m.maxz) dz=center.z-m.maxz;
        float d2 = dx*dx+dy*dy+dz*dz;
        order.emplace_back(d2, i);
    }
    std::sort(order.begin(), order.end(), [](auto &a, auto &b){ return a.first < b.first; });

    std::priority_queue<std::pair<float,KnnCand>> heap;
    for (auto &pr : order) {
        if ((int)heap.size() >= k && pr.first > heap.top().first) break; // 剪枝
        const auto &m = kdleaves[pr.second];
        read_points_block_update_knn(m.file_path, m.offset, m.count, center, k, heap);
    }

    // 输出前 k 个
    std::vector<std::pair<float,KnnCand>> tmp;
    while (!heap.empty()) { tmp.push_back(heap.top()); heap.pop(); }
    std::sort(tmp.begin(), tmp.end(), [](auto &a, auto &b){ return a.first < b.first; });
    size_t take = std::min((size_t)k, tmp.size());
    results.reserve(take);
    for (size_t i=0;i<take;++i) {
        const KnnCand &c = tmp[i].second; float d = std::sqrt(tmp[i].first);
        SimplePoint p{}; p.x=c.x; p.y=c.y; p.z=c.z; p.time=0.0f; p.data_type=TRACE_TYPE_POINTCLOUD; p.fid=(int)c.fid; p.pid=(int)c.row; p.foreign_key=0;
        results.emplace_back(p, d);
    }
    
    return results;
}
