--
-- trace_load_data.sql
-- Test cases for the trace_load_data function
--

-- Load the trace extension
CREATE EXTENSION IF NOT EXISTS trace;

-- Test 1: Check initial state - tables should be empty
SELECT 'Before loading data' as test_phase;
SELECT COUNT(*) as point_cloud_count FROM point_cloud;
SELECT COUNT(*) as mesh_count FROM mesh;

-- Test 2: Use test data from extension data directory

-- Test 3: Load data using trace_load_data function  
SELECT 'Loading test data' as test_phase;
SELECT trace_load_data('/home/zyl/zhangteng/postgres/contrib/trace/data', 10, 1.0) as load_result;

-- Test 4: Verify point cloud data was loaded correctly
SELECT 'Verifying point cloud data' as test_phase;
SELECT COUNT(*) as total_vertices FROM point_cloud;

-- Check specific vertex data (first few vertices)
SELECT 
    file_id,
    vertex_id,
    x,
    y,
    z
FROM point_cloud 
WHERE file_id = 0 AND vertex_id <= 5
ORDER BY vertex_id;

-- Test 5: Verify mesh data was loaded correctly  
SELECT 'Verifying mesh data' as test_phase;
SELECT COUNT(*) as total_faces FROM mesh;

-- Check specific face data (first few faces)
SELECT 
    file_id,
    face_id,
    vertex_count,
    vertex_indices
FROM mesh 
WHERE file_id = 0 AND face_id <= 5
ORDER BY face_id;

-- Test 6: Verify data integrity constraints
SELECT 'Verifying data constraints' as test_phase;

-- Check coordinate statistics
SELECT 
    'Coordinate statistics' as info,
    COUNT(*) as total_vertices,
    MIN(x) as min_x, MAX(x) as max_x,
    MIN(y) as min_y, MAX(y) as max_y,
    MIN(z) as min_z, MAX(z) as max_z
FROM point_cloud;

-- Check that vertex indices in mesh data are valid (should reference existing vertices)
WITH max_vertex AS (
    SELECT file_id, MAX(vertex_id) as max_vertex_id 
    FROM point_cloud 
    GROUP BY file_id
)
SELECT 
    'Invalid vertex references' as issue,
    COUNT(*) as count
FROM mesh m, max_vertex mv
WHERE m.file_id = mv.file_id
AND EXISTS (
    SELECT 1 FROM unnest(m.vertex_indices) as vi 
    WHERE vi > mv.max_vertex_id OR vi < 1
);

-- Test 7: Test edge cases - load with different parameters
SELECT 'Testing with limited file count' as test_phase;
-- Clear previous data first (if function supports it)
-- SELECT trace_clear_data();

-- Load with max_file_num = 1
SELECT trace_load_data('/home/zyl/zhangteng/postgres/contrib/trace/data', 1, 1.0) as load_result_limited;

-- Test 8: Test with sampling ratio
SELECT 'Testing with sampling ratio' as test_phase;
SELECT trace_load_data('/home/zyl/zhangteng/postgres/contrib/trace/data', 10, 0.5) as load_result_sampled;

-- Test 9: Final verification - show summary statistics
SELECT 'Final statistics' as test_phase;
SELECT 
    COUNT(*) as total_point_cloud_records,
    COUNT(DISTINCT file_id) as unique_files_pc
FROM point_cloud;

SELECT 
    COUNT(*) as total_mesh_records,
    COUNT(DISTINCT file_id) as unique_files_mesh,
    AVG(vertex_count) as avg_vertices_per_face
FROM mesh;

-- Test completed
SELECT 'trace_load_data tests completed' as final_status; 