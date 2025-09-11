// 使用统一的安全头文件处理PostgreSQL和libintl.h冲突
#include "../include/safe_header.h"

// 扩展特有的PostgreSQL宏必须在extern "C"块内定义
extern "C" {
// PG_MODULE_MAGIC必须在extern "C"块内部定义
PG_MODULE_MAGIC;

// PostgreSQL function info declarations也必须在extern "C"块内部
PG_FUNCTION_INFO_V1(trace_load_data);
PG_FUNCTION_INFO_V1(trace_build_index);
PG_FUNCTION_INFO_V1(trace_range_query);
PG_FUNCTION_INFO_V1(trace_knn_query);
}

#include "../include/trace.h"

// Global variables
MemoryContext trace_memory_context = NULL;
MemoryContext trace_query_context = NULL;

// GUC variables
static int trace_chunk_max_level = 5;      // Default value
static int trace_octree_max_level = 10;    // Default value  
static int trace_max_point_per_leaf = 1000; // Default value
int trace_bucket_max_points = 8192;  // New GUC for bucket threshold (external linkage)
int trace_kd_leaf_max_points = 4096; // New GUC for kd-leaf threshold (external linkage)

// Static variables
static bool trace_initialized = false;


// Extension initialization
extern "C" void
_PG_init(void)
{
    if (trace_initialized)
        return;
    
    // Create memory contexts
    trace_memory_context = AllocSetContextCreate(TopMemoryContext,
                                                  "TSDMP Extension Context",
                                                  ALLOCSET_DEFAULT_SIZES);
    
    trace_query_context = AllocSetContextCreate(TopMemoryContext,
                                                 "TSDMP Query Context", 
                                                 ALLOCSET_DEFAULT_SIZES);
    
    // Initialize configuration
    initialize_config();
    
    // Define custom GUC variables
    DefineCustomIntVariable("trace.chunk_max_level",
                            "Sets the maximum chunk level for spatial indexing.",
                            "Controls the depth of spatial chunking. Range: 1-10.",
                            &trace_chunk_max_level,
                            5,      // boot value (default)
                            1,      // min value
                            10,     // max value
                            PGC_USERSET,  // can be set by user
                            0,      // flags
                            NULL,   // check_hook
                            NULL,   // assign_hook  
                            NULL);  // show_hook
    
    DefineCustomIntVariable("trace.octree_max_level",
                            "Sets the maximum octree level for spatial indexing.",
                            "Controls the depth of octree subdivision. Range: 1-20.",
                            &trace_octree_max_level,
                            10,     // boot value (default)
                            1,      // min value
                            20,     // max value
                            PGC_USERSET,  // can be set by user
                            0,      // flags
                            NULL,   // check_hook
                            NULL,   // assign_hook
                            NULL);  // show_hook
    
    DefineCustomIntVariable("trace.max_point_per_leaf",
                            "Sets the maximum points per leaf node.",
                            "Controls point density in leaf nodes. Range: 1-10000.",
                            &trace_max_point_per_leaf,
                            1000,   // boot value (default)
                            1,      // min value
                            10000,  // max value
                            PGC_USERSET,  // can be set by user
                            0,      // flags
                            NULL,   // check_hook
                            NULL,   // assign_hook
                            NULL);  // show_hook
    
    DefineCustomIntVariable("trace.bucket_max_points",
                            "Sets the maximum points per inner bucket.",
                            "Controls inner octree bucket size. Range: 1-100000.",
                            &trace_bucket_max_points,
                            8192,   // boot value (default)
                            1,      // min value
                            100000, // max value
                            PGC_USERSET,
                            0,
                            NULL,
                            NULL,
                            NULL);
    
    DefineCustomIntVariable("trace.kd_leaf_max_points",
                            "Sets the maximum points per kd leaf.",
                            "Controls kd-leaf size inside buckets. Range: 1-100000.",
                            &trace_kd_leaf_max_points,
                            4096,   // boot value (default)
                            1,      // min value
                            100000, // max value
                            PGC_USERSET,
                            0,
                            NULL,
                            NULL,
                            NULL);
    
    trace_initialized = true;
    
    elog(DEBUG1, "TSDMP Extension initialized successfully");
}

// Extension cleanup
extern "C" void
_PG_fini(void)
{
    if (!trace_initialized)
        return;
    
    trace_initialized = false;
    
    elog(INFO, "TSDMP Extension finalized");
}

// DatasetBounds and get_current_dataset_bounds have been moved to trace.cpp

// Data loading function
extern "C" Datum
trace_load_data(PG_FUNCTION_ARGS)
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
    
    MemoryContext old_context = MemoryContextSwitchTo(trace_memory_context);
    
    try {
        // Extract source directory
        char *source_dir_cstr = text_to_cstring(source_dir_text);
        std::string directory(source_dir_cstr);
        
        // Call main implementation
        LoadResult result = trace_load_data_impl(directory, max_file_num, sample_ratio);
        
        // Create return tuple
        TupleDesc tupdesc;
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context that cannot accept a record")));
        
        HeapTuple result_tuple = trace::create_load_result_tuple(result, tupdesc);
        
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
// This function uses GUC variables for configuration.
// To change parameters, use:
// SET trace.chunk_max_level = 8;
// SET trace.octree_max_level = 15;
// SET trace.max_point_per_leaf = 2000;
extern "C" Datum
trace_build_index(PG_FUNCTION_ARGS)
{
    elog(DEBUG1, "Building index with GUC parameters: chunk_max_level=%d, octree_max_level=%d, max_point_per_leaf=%d",
         trace_chunk_max_level, trace_octree_max_level, trace_max_point_per_leaf);
    
    // Use GUC variables directly (validation is handled by GUC system)
    MemoryContext old_context = MemoryContextSwitchTo(trace_memory_context);
    
    try {
        // Call main implementation using GUC variables
        IndexResult result = trace_build_index_impl(trace_chunk_max_level, 
                                                    trace_octree_max_level, 
                                                    trace_max_point_per_leaf);
        
        // Create return tuple
        TupleDesc tupdesc;
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context that cannot accept a record")));
        
        HeapTuple result_tuple = trace::create_index_result_tuple(result, tupdesc);
        
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
trace_range_query(PG_FUNCTION_ARGS)
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
    
    MemoryContext old_context = MemoryContextSwitchTo(trace_query_context);
    FuncCallContext *funcctx;
    
    try {
        // Create query bounds
        SimpleBounds query_bounds = trace::bounds_from_pg_args(min_x, min_y, min_z, 
                                                              max_x, max_y, max_z,
                                                              min_time, max_time);
        
        if (SRF_IS_FIRSTCALL()) {
            funcctx = SRF_FIRSTCALL_INIT();
            
            MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
            
            // Call main implementation
            std::vector<SimplePoint> results = trace_range_query_impl(query_bounds, data_type_mask);
            
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
            
            HeapTuple tuple = trace::create_spatiotemporal_point_tuple(point, tupdesc);
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
trace_knn_query(PG_FUNCTION_ARGS)
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
    
    MemoryContext old_context = MemoryContextSwitchTo(trace_query_context);
    FuncCallContext *funcctx;
    
    try {
        // Create query point
        SimplePoint query_point = trace::point_from_pg_args(center_x, center_y, center_z, 0.0);
        
        if (SRF_IS_FIRSTCALL()) {
            funcctx = SRF_FIRSTCALL_INIT();
            
            MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
            
            // Call main implementation
            std::vector<std::pair<SimplePoint, float>> knn_results = 
                trace_knn_query_impl(query_point, k, min_time, max_time, data_type_mask);
            
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
            
            HeapTuple tuple = trace::create_knn_result_tuple(result, tupdesc);
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
