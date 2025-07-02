// 使用统一的安全头文件处理PostgreSQL和libintl.h冲突
#include "../include/safe_header.h"

// 扩展特有的PostgreSQL宏必须在extern "C"块内定义
extern "C" {
// PG_MODULE_MAGIC必须在extern "C"块内部定义
PG_MODULE_MAGIC;

// PostgreSQL function info declarations也必须在extern "C"块内部
PG_FUNCTION_INFO_V1(tsdmp_load_data);
PG_FUNCTION_INFO_V1(tsdmp_build_index);
PG_FUNCTION_INFO_V1(tsdmp_range_query);
PG_FUNCTION_INFO_V1(tsdmp_knn_query);
PG_FUNCTION_INFO_V1(tsdmp_clear_data);
PG_FUNCTION_INFO_V1(tsdmp_get_stats);
PG_FUNCTION_INFO_V1(tsdmp_set_config);
PG_FUNCTION_INFO_V1(tsdmp_get_config);
}

#include "../include/tsdmp.h"

// Global variables
MemoryContext tsdmp_memory_context = NULL;
MemoryContext tsdmp_query_context = NULL;

// Global variable definitions (declared as extern in header)
SimpleBounds global_bounds;
std::vector<std::string> data_source_files;
std::map<std::string, std::string> config_map;

// Static variables
static bool tsdmp_initialized = false;

// Extension initialization
extern "C" void
_PG_init(void)
{
    if (tsdmp_initialized)
        return;
    
    // Create memory contexts
    tsdmp_memory_context = AllocSetContextCreate(TopMemoryContext,
                                                  "TSDMP Extension Context",
                                                  ALLOCSET_DEFAULT_SIZES);
    
    tsdmp_query_context = AllocSetContextCreate(TopMemoryContext,
                                                 "TSDMP Query Context", 
                                                 ALLOCSET_DEFAULT_SIZES);
    
    // Initialize configuration
    initialize_config();
    
    tsdmp_initialized = true;
    
    elog(INFO, "TSDMP Extension initialized successfully");
}

// Extension cleanup
extern "C" void
_PG_fini(void)
{
    if (!tsdmp_initialized)
        return;
    
    tsdmp_initialized = false;
    
    elog(INFO, "TSDMP Extension finalized");
}

// Data loading function
extern "C" Datum
tsdmp_load_data(PG_FUNCTION_ARGS)
{
    text *source_dir_text = PG_GETARG_TEXT_PP(0);
    int32 max_file_num = PG_GETARG_INT32(1);
    float4 sample_ratio = PG_GETARG_FLOAT4(2);
    
    // Validate arguments
    if (max_file_num <= 0)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("max_file_num must be positive")));
    
    if (sample_ratio <= 0.0 || sample_ratio > 1.0)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("sample_ratio must be between 0 and 1")));
    
    MemoryContext old_context = MemoryContextSwitchTo(tsdmp_memory_context);
    
    try {
        // Extract source directory
        char *source_dir_cstr = text_to_cstring(source_dir_text);
        std::string directory(source_dir_cstr);
        
        // Call main implementation
        LoadResult result = tsdmp_load_data_impl(directory, max_file_num, sample_ratio);
        
        // Create return tuple
        TupleDesc tupdesc;
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context that cannot accept a record")));
        
        HeapTuple result_tuple = tsdmp::create_load_result_tuple(result, tupdesc);
        
        MemoryContextSwitchTo(old_context);
        PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
        
    } catch (const std::exception& e) {
        MemoryContextSwitchTo(old_context);
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("error loading data: %s", e.what())));
    }
    
    MemoryContextSwitchTo(old_context);
    PG_RETURN_NULL();
}

// Index building function
extern "C" Datum
tsdmp_build_index(PG_FUNCTION_ARGS)
{
    int32 chunk_max_level_param = PG_GETARG_INT32(0);
    int32 octree_max_level_param = PG_GETARG_INT32(1);
    int32 max_point_per_leaf_param = PG_GETARG_INT32(2);
    
    // Validate arguments
    if (chunk_max_level_param < 1 || chunk_max_level_param > 10)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("chunk_max_level must be between 1 and 10")));
    
    if (octree_max_level_param < 1 || octree_max_level_param > 20)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("octree_max_level must be between 1 and 20")));
    
    if (max_point_per_leaf_param < 1 || max_point_per_leaf_param > 10000)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("max_point_per_leaf must be between 1 and 10000")));
    
    MemoryContext old_context = MemoryContextSwitchTo(tsdmp_memory_context);
    
    try {
        // Call main implementation
        IndexResult result = tsdmp_build_index_impl(chunk_max_level_param, 
                                                    octree_max_level_param, 
                                                    max_point_per_leaf_param);
        
        // Create return tuple
        TupleDesc tupdesc;
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context that cannot accept a record")));
        
        HeapTuple result_tuple = tsdmp::create_index_result_tuple(result, tupdesc);
        
        MemoryContextSwitchTo(old_context);
        PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
        
    } catch (const std::exception& e) {
        MemoryContextSwitchTo(old_context);
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("error building index: %s", e.what())));
    }
    
    MemoryContextSwitchTo(old_context);
    PG_RETURN_NULL();
}

// Range query function
extern "C" Datum
tsdmp_range_query(PG_FUNCTION_ARGS)
{
    float4 min_x = PG_GETARG_FLOAT4(0);
    float4 min_y = PG_GETARG_FLOAT4(1);
    float4 min_z = PG_GETARG_FLOAT4(2);
    float4 max_x = PG_GETARG_FLOAT4(3);
    float4 max_y = PG_GETARG_FLOAT4(4);
    float4 max_z = PG_GETARG_FLOAT4(5);
    float4 min_time = PG_GETARG_FLOAT4(6);
    float4 max_time = PG_GETARG_FLOAT4(7);
    int32 data_type_mask = PG_GETARG_INT32(8);
    
    // Validate query bounds
    if (min_x > max_x || min_y > max_y || min_z > max_z || min_time > max_time) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("invalid query bounds: min values must be <= max values")));
    }
    
    MemoryContext old_context = MemoryContextSwitchTo(tsdmp_query_context);
    FuncCallContext *funcctx;
    
    try {
        // Create query bounds
        SimpleBounds query_bounds = tsdmp::bounds_from_pg_args(min_x, min_y, min_z, 
                                                              max_x, max_y, max_z,
                                                              min_time, max_time);
        
        if (SRF_IS_FIRSTCALL()) {
            funcctx = SRF_FIRSTCALL_INIT();
            
            MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
            
            // Call main implementation
            std::vector<SimplePoint> results = tsdmp_range_query_impl(query_bounds, data_type_mask);
            
            // Store results in function context
            funcctx->max_calls = results.size();
            if (results.size() > 0) {
                funcctx->user_fctx = palloc(sizeof(SimplePoint) * results.size());
                memcpy(funcctx->user_fctx, results.data(), sizeof(SimplePoint) * results.size());
            } else {
                funcctx->user_fctx = NULL;
            }
            
            MemoryContextSwitchTo(oldcontext);
        }
        
        funcctx = SRF_PERCALL_SETUP();
        
        if (funcctx->call_cntr < funcctx->max_calls) {
            SimplePoint* stored_results = (SimplePoint*)funcctx->user_fctx;
            SimplePoint point = stored_results[funcctx->call_cntr];
            
            // Create tuple for this point
            TupleDesc tupdesc;
            if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
                ereport(ERROR,
                        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                         errmsg("function returning record called in context that cannot accept a record")));
            
            HeapTuple tuple = tsdmp::create_spatiotemporal_point_tuple(point, tupdesc);
            MemoryContextSwitchTo(old_context);
            SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
        } else {
            MemoryContextSwitchTo(old_context);
            SRF_RETURN_DONE(funcctx);
        }
        
    } catch (const std::exception& e) {
        MemoryContextSwitchTo(old_context);
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("error in range query: %s", e.what())));
    }
    
    MemoryContextSwitchTo(old_context);
    PG_RETURN_NULL();
}

// kNN query function
extern "C" Datum
tsdmp_knn_query(PG_FUNCTION_ARGS)
{
    float4 center_x = PG_GETARG_FLOAT4(0);
    float4 center_y = PG_GETARG_FLOAT4(1);
    float4 center_z = PG_GETARG_FLOAT4(2);
    int32 k = PG_GETARG_INT32(3);
    float4 min_time = PG_GETARG_FLOAT4(4);
    float4 max_time = PG_GETARG_FLOAT4(5);
    int32 data_type_mask = PG_GETARG_INT32(6);
    
    // Validate parameters
    if (k <= 0 || k > 10000) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("k must be between 1 and 10000")));
    }
    
    if (min_time > max_time) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("min_time must be <= max_time")));
    }
    
    MemoryContext old_context = MemoryContextSwitchTo(tsdmp_query_context);
    FuncCallContext *funcctx;
    
    try {
        // Create query point
        SimplePoint query_point = tsdmp::point_from_pg_args(center_x, center_y, center_z, 0.0);
        
        if (SRF_IS_FIRSTCALL()) {
            funcctx = SRF_FIRSTCALL_INIT();
            
            MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
            
            // Call main implementation
            std::vector<std::pair<SimplePoint, float>> knn_results = 
                tsdmp_knn_query_impl(query_point, k, min_time, max_time, data_type_mask);
            
            // Convert to KnnResult format and store in function context
            funcctx->max_calls = knn_results.size();
            if (knn_results.size() > 0) {
                KnnResult* results = (KnnResult*)palloc(sizeof(KnnResult) * knn_results.size());
                for (size_t i = 0; i < knn_results.size(); i++) {
                    results[i].point = knn_results[i].first;
                    results[i].distance = knn_results[i].second;
                }
                funcctx->user_fctx = results;
            } else {
                funcctx->user_fctx = NULL;
            }
            
            MemoryContextSwitchTo(oldcontext);
        }
        
        funcctx = SRF_PERCALL_SETUP();
        
        if (funcctx->call_cntr < funcctx->max_calls) {
            KnnResult* stored_results = (KnnResult*)funcctx->user_fctx;
            KnnResult result = stored_results[funcctx->call_cntr];
            
            // Create tuple for this result
            TupleDesc tupdesc;
            if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
                ereport(ERROR,
                        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                         errmsg("function returning record called in context that cannot accept a record")));
            
            HeapTuple tuple = tsdmp::create_knn_result_tuple(result, tupdesc);
            MemoryContextSwitchTo(old_context);
            SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
        } else {
            MemoryContextSwitchTo(old_context);
            SRF_RETURN_DONE(funcctx);
        }
        
    } catch (const std::exception& e) {
        MemoryContextSwitchTo(old_context);
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("error in kNN query: %s", e.what())));
    }
    
    MemoryContextSwitchTo(old_context);
    PG_RETURN_NULL();
}

// Configuration functions
extern "C" Datum
tsdmp_set_config(PG_FUNCTION_ARGS)
{
    text *key_text = PG_GETARG_TEXT_PP(0);
    text *value_text = PG_GETARG_TEXT_PP(1);
    
    char *key = text_to_cstring(key_text);
    char *value = text_to_cstring(value_text);
    
    try {
        tsdmp_set_config_impl(std::string(key), std::string(value));
        elog(INFO, "Set config: %s = %s", key, value);
        PG_RETURN_BOOL(true);
    } catch (const std::exception& e) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("error setting config: %s", e.what())));
    }
    
    PG_RETURN_BOOL(false);
}

extern "C" Datum
tsdmp_get_config(PG_FUNCTION_ARGS)
{
    text *key_text = PG_GETARG_TEXT_PP(0);
    char *key = text_to_cstring(key_text);
    
    try {
        std::string value = tsdmp_get_config_impl(std::string(key));
        PG_RETURN_TEXT_P(cstring_to_text(value.c_str()));
    } catch (const std::exception& e) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("error getting config: %s", e.what())));
    }
    
    PG_RETURN_NULL();
}

// Clear data function
extern "C" Datum
tsdmp_clear_data(PG_FUNCTION_ARGS)
{
    try {
        bool success = tsdmp_clear_data_impl();
        elog(INFO, "Data cleared successfully");
        PG_RETURN_BOOL(success);
    } catch (const std::exception& e) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("error clearing data: %s", e.what())));
    }
    
    PG_RETURN_BOOL(false);
}

// Statistics function
extern "C" Datum
tsdmp_get_stats(PG_FUNCTION_ARGS)
{
    try {
        // Call main implementation
        StatsResult stats = tsdmp_get_stats_impl();
        
        // Create return tuple
        TupleDesc tupdesc;
        Datum values[4];
        bool nulls[4] = {false, false, false, false};
        
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context that cannot accept a record")));
        
        values[0] = Int32GetDatum(stats.total_files);
        values[1] = Int64GetDatum(stats.total_points);
        values[2] = Int32GetDatum(stats.total_chunks);
        values[3] = Float4GetDatum(stats.index_size_mb);
        
        HeapTuple result_tuple = heap_form_tuple(tupdesc, values, nulls);
        PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
        
    } catch (const std::exception& e) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("error getting statistics: %s", e.what())));
    }
    
    PG_RETURN_NULL();
} 