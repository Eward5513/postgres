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

SimpleBounds global_bounds;
std::vector<std::string> data_source_files;
std::map<std::string, std::string> config_map;


// Morton 辅助函数改为独立文件 morton_utils.{h,cpp}

// 持久化在 index_storage.cpp 中实现

// LeafMeta 结构体已在 index_storage.h 中定义

// DB/磁盘辅助函数在 index_storage.{h,cpp}

//


namespace trace {



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
    // 记录数据集路径到配置，供持久化索引使用
    config_map["dataset_path"] = directory;
    
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
    
    // 使用 IndexBuilder 完成构建与持久化
    auto build_start = std::chrono::high_resolution_clock::now();
    IndexBuilder::Params p{ octree_max_level_param, max_point_per_leaf_param, 8192, 6, 4096 };
    IndexBuilder builder(config_map["dataset_path"], global_bounds, data_source_files, p);
    builder.build_all();
    auto build_end = std::chrono::high_resolution_clock::now();
    result.index_build_time = std::chrono::duration<float>(build_end - build_start).count();
    result.chunk_count = (int)data_source_files.size();
    result.total_octree_nodes = 0;
    result.total_kdtree_nodes = 0;
    
    return result;
}

// Range query implementation
std::vector<SimplePoint> trace_range_query_impl(const SimpleBounds& bounds, int data_type_mask)
{
    std::vector<SimplePoint> results;
    
    // 使用桶（叶内 octree）粗筛，直接按 bucket 偏移读取
    const std::string dataset_path = config_map["dataset_path"];
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

    // 使用 kd 叶（桶内二分）的包围盒做粗序，从近到远读取块更新堆
    const std::string dataset_path = config_map["dataset_path"];
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
