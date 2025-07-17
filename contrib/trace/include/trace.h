#ifndef TRACE_H
#define TRACE_H

#include "safe_header.h"

// Standard C++ headers
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>

// Simplified data structures (independent of TRACE)
struct SimpleBounds {
    float min_x, min_y, min_z, min_time;
    float max_x, max_y, max_z, max_time;
    
    SimpleBounds() : min_x(0), min_y(0), min_z(0), min_time(0),
                     max_x(0), max_y(0), max_z(0), max_time(0) {}
    
    SimpleBounds(float minx, float miny, float minz, float mint,
                 float maxx, float maxy, float maxz, float maxt)
        : min_x(minx), min_y(miny), min_z(minz), min_time(mint),
          max_x(maxx), max_y(maxy), max_z(maxz), max_time(maxt) {}
};

struct SimplePoint {
    float x, y, z, time;
    int data_type;
    int fid, pid, foreign_key;
    float intensity, speed;
    int color_r, color_g, color_b;
    
    SimplePoint() : x(0), y(0), z(0), time(0), data_type(0),
                    fid(0), pid(0), foreign_key(0),
                    intensity(0), speed(0),
                    color_r(0), color_g(0), color_b(0) {}
};

struct LoadResult {
    int files_loaded;
    long long total_points;
    float load_time_seconds;
    
    LoadResult() : files_loaded(0), total_points(0), load_time_seconds(0.0f) {}
};

struct IndexResult {
    float index_build_time;
    int chunk_count;
    int total_octree_nodes;
    int total_kdtree_nodes;
    
    IndexResult() : index_build_time(0.0f), chunk_count(0),
                    total_octree_nodes(0), total_kdtree_nodes(0) {}
};

struct StatsResult {
    int total_files;
    long long total_points;
    int total_chunks;
    float index_size_mb;
    
    StatsResult() : total_files(0), total_points(0),
                    total_chunks(0), index_size_mb(0.0f) {}
};

// PostgreSQL interface declarations
extern "C" {

// Memory context management
extern MemoryContext trace_memory_context;
extern MemoryContext trace_query_context;

// Global variables (independent of TRACE)
extern bool trace_index_built;
extern std::string trace_data_directory;
extern int trace_max_files;
extern float trace_sample_ratio;

// Main extension functions (C interface for PostgreSQL)
Datum trace_load_data(PG_FUNCTION_ARGS);
Datum trace_build_index(PG_FUNCTION_ARGS);
Datum trace_range_query(PG_FUNCTION_ARGS);
Datum trace_knn_query(PG_FUNCTION_ARGS);
Datum trace_clear_data(PG_FUNCTION_ARGS);
Datum trace_get_stats(PG_FUNCTION_ARGS);
Datum trace_set_config(PG_FUNCTION_ARGS);
Datum trace_get_config(PG_FUNCTION_ARGS);

} // extern "C"

// C++ namespace for internal implementation
namespace trace {

// Utility functions for PostgreSQL integration
TupleDesc createTupleDesc(const std::vector<std::string>& column_names,
                         const std::vector<Oid>& column_types);

char* allocate_string(const std::string& str);
bytea* allocate_bytea(const void* data, size_t size);
TupleDesc trace_TypeGetTupleDesc(Oid type_oid, List* coldeflist);

// Logging functions
void trace_error(const char* fmt, ...);
void trace_warning(const char* fmt, ...);
void trace_info(const char* fmt, ...);

// Memory management functions
void* palloc_in_context(Size size, MemoryContext context);
void pfree_in_context(void* ptr, MemoryContext context);
char* pstrdup_in_context(const char* str, MemoryContext context);

// Array utilities
char** extract_string_array(ArrayType* array, int* n_elements);

// Database utilities
void execute_sql(const char* sql);
bool table_exists(const char* table_name);

} // namespace trace

// Implementation functions (C++ interface)
LoadResult trace_load_data_impl(const std::string& directory, int max_file_num, float sample_ratio);
IndexResult trace_build_index_impl(int chunk_max_level, int octree_max_level, int max_point_per_leaf);
std::vector<SimplePoint> trace_range_query_impl(const SimpleBounds& bounds, int data_type_mask);
std::vector<std::pair<SimplePoint, float>> trace_knn_query_impl(const SimplePoint& center, int k,
                                                                float min_time, float max_time, int data_type_mask);

void trace_set_config_impl(const std::string& key, const std::string& value);
std::string trace_get_config_impl(const std::string& key);
bool trace_clear_data_impl();
StatsResult trace_get_stats_impl();

// Error codes
#define TRACE_SUCCESS 0
#define TRACE_ERROR_INVALID_ARGS -1
#define TRACE_ERROR_FILE_NOT_FOUND -2
#define TRACE_ERROR_MEMORY -3
#define TRACE_ERROR_DATABASE -4
#define TRACE_ERROR_INDEX_NOT_BUILT -5

// Data type constants
#define TRACE_TYPE_POINTCLOUD 1
#define TRACE_TYPE_MESH 2
#define TRACE_TYPE_TRAJECTORY 4

#endif // TRACE_H 