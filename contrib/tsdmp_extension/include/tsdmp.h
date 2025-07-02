#ifndef TSDMP_H
#define TSDMP_H

// Standard C++ headers
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>

// PostgreSQL headers
extern "C" {
#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/lsyscache.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "access/htup_details.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "storage/proc.h"
#include "postmaster/bgworker.h"
#include "catalog/namespace.h"
#include "parser/parse_type.h"
#include "utils/typcache.h"
#include "utils/errcodes.h"
#include "nodes/makefuncs.h"
#include "parser/parser.h"
}

// Simplified data structures (independent of TSDMP)
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
    
    SimplePoint() : x(0), y(0), z(0), time(0), data_type(0), 
                    fid(0), pid(0), foreign_key(0) {}
    
    SimplePoint(float px, float py, float pz, float pt)
        : x(px), y(py), z(pz), time(pt), data_type(0),
          fid(0), pid(0), foreign_key(0) {}
};

// Load result structure for PostgreSQL
typedef struct {
    int32 files_loaded;
    int64 total_points;
    SimpleBounds global_bounds;
    float load_time_seconds;
} LoadResult;

// Statistics result structure
typedef struct {
    int32 total_files;
    int64 total_points;
    int32 total_chunks;
    float index_size_mb;
} StatsResult;

// Index result structure for PostgreSQL
typedef struct {
    float index_build_time;
    int32 chunk_count;
    int32 total_octree_nodes;
    int32 total_kdtree_nodes;
} IndexResult;

// kNN result structure for PostgreSQL
typedef struct {
    float distance;
    SimplePoint point;
} KnnResult;

// Memory context management
extern MemoryContext tsdmp_memory_context;
extern MemoryContext tsdmp_query_context;

// Global variables (independent of TSDMP)
extern SimpleBounds global_bounds;
extern std::vector<std::string> data_source_files;
extern std::map<std::string, std::string> config_map;

// Function declarations
extern "C" {
    // Extension lifecycle
    void _PG_init(void);
    void _PG_fini(void);
    
    // Function implementations
    Datum tsdmp_load_data(PG_FUNCTION_ARGS);
    Datum tsdmp_build_index(PG_FUNCTION_ARGS);
    Datum tsdmp_range_query(PG_FUNCTION_ARGS);
    Datum tsdmp_knn_query(PG_FUNCTION_ARGS);
    Datum tsdmp_clear_data(PG_FUNCTION_ARGS);
    Datum tsdmp_get_stats(PG_FUNCTION_ARGS);
    Datum tsdmp_set_config(PG_FUNCTION_ARGS);
    Datum tsdmp_get_config(PG_FUNCTION_ARGS);
}

// Utility functions (to be implemented)
namespace tsdmp {
    // Memory management
    void* palloc_in_context(Size size, MemoryContext context);
    void pfree_in_context(void* ptr, MemoryContext context);
    char* pstrdup_in_context(const char* str, MemoryContext context);
    
    // Array utilities
    char** extract_string_array(ArrayType* array, int* n_elements);
    
    // Database utilities
    void execute_sql(const char* sql);
    SPITupleTable* execute_sql_select(const char* sql);
    
    // Configuration management
    void load_config();
    void save_config();
    std::string get_config_value(const std::string& key);
    void set_config_value(const std::string& key, const std::string& value);
    
    // Type conversion utilities
    Oid get_type_oid(const char* type_name);
    TupleDesc tsdmp_TypeGetTupleDesc(Oid type_oid, List* coldeflist);
    
    // Error handling
    void tsdmp_error(const char* fmt, ...);
    void tsdmp_warning(const char* fmt, ...);
    void tsdmp_info(const char* fmt, ...);
    
    // Convert between PostgreSQL types and internal types
    SimpleBounds bounds_from_pg_args(float min_x, float min_y, float min_z, 
                                    float max_x, float max_y, float max_z,
                                    float min_time, float max_time);
    SimplePoint point_from_pg_args(float x, float y, float z, float time);
    
    // PostgreSQL result creation
    HeapTuple create_load_result_tuple(const LoadResult& result, TupleDesc tupdesc);
    HeapTuple create_index_result_tuple(const IndexResult& result, TupleDesc tupdesc);
    HeapTuple create_spatiotemporal_point_tuple(const SimplePoint& point, TupleDesc tupdesc);
    HeapTuple create_knn_result_tuple(const KnnResult& result, TupleDesc tupdesc);
}

// Main implementation function declarations (implemented in main.cpp)
void initialize_config();
LoadResult tsdmp_load_data_impl(const std::string& directory, int max_file_num, float sample_ratio);
IndexResult tsdmp_build_index_impl(int chunk_max_level, int octree_max_level, int max_point_per_leaf);
std::vector<SimplePoint> tsdmp_range_query_impl(const SimpleBounds& bounds, int data_type_mask);
std::vector<std::pair<SimplePoint, float>> tsdmp_knn_query_impl(const SimplePoint& center, int k, 
                                                                float min_time, float max_time, 
                                                                int data_type_mask);
void tsdmp_set_config_impl(const std::string& key, const std::string& value);
std::string tsdmp_get_config_impl(const std::string& key);
bool tsdmp_clear_data_impl();
StatsResult tsdmp_get_stats_impl();

// Constants
#define TSDMP_SUCCESS 0
#define TSDMP_ERROR_INVALID_ARGS -1
#define TSDMP_ERROR_FILE_NOT_FOUND -2
#define TSDMP_ERROR_MEMORY -3
#define TSDMP_ERROR_DATABASE -4
#define TSDMP_ERROR_INDEX_NOT_BUILT -5

// Data type constants
#define TSDMP_TYPE_POINTCLOUD 1
#define TSDMP_TYPE_MESH 2
#define TSDMP_TYPE_TRAJECTORY 4

#endif // TSDMP_H 