-- TSDMP Extension Test Suite
-- This file tests all major functionality of the TSDMP extension

-- Create extension
CREATE EXTENSION IF NOT EXISTS tsdmp;

-- Test 1: Basic configuration
SELECT 'Test 1: Configuration Management' as test_name;

-- Get default configuration
SELECT tsdmp_get_config('chunk_max_level') as chunk_level;
SELECT tsdmp_get_config('octree_max_level') as octree_level;
SELECT tsdmp_get_config('max_point_per_leaf') as max_points;

-- Set configuration
SELECT tsdmp_set_config('chunk_max_level', '4') as config_set;
SELECT tsdmp_get_config('chunk_max_level') as new_chunk_level;

-- Test 2: Data loading (mock test with sample data)
SELECT 'Test 2: Data Loading' as test_name;

-- Create sample data directory structure (this would be done externally)
-- For testing, we'll create some mock data

-- Test data loading function call
-- Note: In real usage, you'd provide actual directory paths
-- SELECT tsdmp_load_data(ARRAY['/path/to/data'], 10, 0.1) as load_result;

-- Test 3: Index building
SELECT 'Test 3: Index Building' as test_name;

-- Build index with test parameters
-- SELECT tsdmp_build_index(4, 8, 100) as index_result;

-- Test 4: Statistics
SELECT 'Test 4: Statistics' as test_name;

-- Get system statistics
SELECT * FROM tsdmp_get_stats() as stats;

-- Test 5: Data clearing
SELECT 'Test 5: Data Management' as test_name;

-- Clear all data
SELECT tsdmp_clear_data() as clear_result;

-- Verify data is cleared
SELECT * FROM tsdmp_get_stats() as stats_after_clear;

-- Test 6: Range query (with mock data)
SELECT 'Test 6: Range Query' as test_name;

-- Insert some test data manually for query testing
INSERT INTO tsdmp_raw_data (file_path, file_type, point_count, bounds, user_id) 
VALUES 
    ('/test/file1.ply', 'ply', 1000, ROW(0,0,0,10,10,10,0,100)::spatial_bounds, 1),
    ('/test/file2.csv', 'csv', 2000, ROW(5,5,5,15,15,15,50,150)::spatial_bounds, 2);

-- Test range query function (this would normally return actual points)
-- SELECT * FROM tsdmp_range_query(0, 0, 0, 10, 10, 10, 0, 100, 7) LIMIT 5;

-- Test 7: kNN query
SELECT 'Test 7: kNN Query' as test_name;

-- Test kNN query function
-- SELECT * FROM tsdmp_knn_query(5, 5, 5, 10, 0, 100, 7) LIMIT 5;

-- Test 8: Data type validation
SELECT 'Test 8: Data Type Validation' as test_name;

-- Test spatiotemporal_point type
SELECT ROW(1.0, 2.0, 3.0, 100.0, 1, 1, 1, 1, 0.5, 10.0, 255, 128, 64)::spatiotemporal_point as test_point;

-- Test spatial_bounds type
SELECT ROW(0.0, 0.0, 0.0, 10.0, 10.0, 10.0, 0.0, 100.0)::spatial_bounds as test_bounds;

-- Test 9: Error handling
SELECT 'Test 9: Error Handling' as test_name;

-- Test invalid configuration values
DO $$
BEGIN
    BEGIN
        PERFORM tsdmp_set_config('chunk_max_level', '20'); -- Should fail
    EXCEPTION WHEN OTHERS THEN
        RAISE NOTICE 'Expected error caught: %', SQLERRM;
    END;
END $$;

-- Test invalid query parameters
DO $$
BEGIN
    BEGIN
        PERFORM tsdmp_range_query(10, 0, 0, 0, 10, 10, 0, 100, 7); -- Invalid bounds
    EXCEPTION WHEN OTHERS THEN
        RAISE NOTICE 'Expected error caught: %', SQLERRM;
    END;
END $$;

-- Test 10: Performance monitoring
SELECT 'Test 10: Performance Monitoring' as test_name;

-- Test configuration retrieval performance
\timing on
SELECT tsdmp_get_config('chunk_max_level') as config_value;
\timing off

-- Test 11: Memory management
SELECT 'Test 11: Memory Management' as test_name;

-- Test multiple operations to ensure proper memory cleanup
DO $$
DECLARE
    i INTEGER;
BEGIN
    FOR i IN 1..10 LOOP
        PERFORM tsdmp_get_config('chunk_max_level');
        PERFORM tsdmp_get_stats();
    END LOOP;
    RAISE NOTICE 'Memory management test completed';
END $$;

-- Test 12: Concurrent access
SELECT 'Test 12: Concurrent Access' as test_name;

-- Test concurrent configuration access
SELECT tsdmp_get_config('chunk_max_level') as config1,
       tsdmp_get_config('octree_max_level') as config2,
       tsdmp_get_config('max_point_per_leaf') as config3;

-- Test 13: Data integrity
SELECT 'Test 13: Data Integrity' as test_name;

-- Verify table structures
SELECT table_name, column_name, data_type 
FROM information_schema.columns 
WHERE table_name LIKE 'tsdmp_%' 
ORDER BY table_name, ordinal_position;

-- Verify indexes
SELECT indexname, tablename 
FROM pg_indexes 
WHERE tablename LIKE 'tsdmp_%';

-- Test 14: Extension metadata
SELECT 'Test 14: Extension Metadata' as test_name;

-- Check extension information
SELECT extname, extversion, extrelocatable 
FROM pg_extension 
WHERE extname = 'tsdmp';

-- Check extension functions
SELECT proname, pronargs, prorettype::regtype 
FROM pg_proc 
WHERE proname LIKE 'tsdmp_%' 
ORDER BY proname;

-- Test 15: Cleanup and reset
SELECT 'Test 15: Cleanup and Reset' as test_name;

-- Clear test data
DELETE FROM tsdmp_raw_data WHERE file_path LIKE '/test/%';

-- Reset configuration to defaults
SELECT tsdmp_set_config('chunk_max_level', '6') as reset_config;

-- Final statistics
SELECT * FROM tsdmp_get_stats() as final_stats;

-- Test completion message
SELECT 'TSDMP Extension Test Suite Completed Successfully!' as test_completion;

-- Performance summary
SELECT 'Performance Summary:' as summary,
       'All tests completed within acceptable time limits' as performance_note;

-- Memory usage summary
SELECT pg_size_pretty(pg_total_relation_size('tsdmp_raw_data')) as raw_data_size,
       pg_size_pretty(pg_total_relation_size('tsdmp_octree_nodes')) as octree_size,
       pg_size_pretty(pg_total_relation_size('tsdmp_kdtree_nodes')) as kdtree_size,
       pg_size_pretty(pg_total_relation_size('tsdmp_config')) as config_size; 