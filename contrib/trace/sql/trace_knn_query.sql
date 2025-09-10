--
-- trace_knn_query.sql
-- kNN查询测试
--

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

-- 加载trace扩展
CREATE EXTENSION IF NOT EXISTS trace;

-- 测试1: 准备数据和索引
SELECT 'Preparing data and index for kNN queries' as test_phase;


-- 加载测试数据
\set datadir :abs_srcdir '/data'
SELECT trace_load_data(:'datadir', 3, 1.0) as load_result;

-- 构建索引
SELECT trace_build_index() as index_result;

-- 测试2: 基本kNN查询
SELECT 'Testing basic kNN queries' as test_phase;

-- 查询最近的5个点（从第一个数据集中心附近开始）
SELECT COUNT(*) as knn_5_count
FROM trace_knn_query(116.3978, 39.9046, 10.9, 5, '-infinity'::REAL, 'infinity'::REAL, 7);

-- 查询最近的3个点
SELECT 
    distance,
    (point).x, (point).y, (point).z, (point).time_stamp,
    (point).data_type, (point).file_id, (point).point_id
FROM trace_knn_query(116.3978, 39.9046, 10.9, 3, '-infinity'::REAL, 'infinity'::REAL, 7)
ORDER BY distance;

-- 测试3: 不同k值测试
SELECT 'Testing different k values' as test_phase;

-- k=1 (最近的1个点)
SELECT COUNT(*) as knn_1_count
FROM trace_knn_query(116.3975, 39.9043, 10.6, 1, '-infinity'::REAL, 'infinity'::REAL, 7);

-- k=10 (最近的10个点)
SELECT COUNT(*) as knn_10_count  
FROM trace_knn_query(116.3975, 39.9043, 10.6, 10, '-infinity'::REAL, 'infinity'::REAL, 7);

-- k=20 (如果总点数少于20，应该返回所有点)
SELECT COUNT(*) as knn_20_count
FROM trace_knn_query(116.3975, 39.9043, 10.6, 20, '-infinity'::REAL, 'infinity'::REAL, 7);

-- 测试4: 时间范围过滤的kNN查询
SELECT 'Testing kNN with time range filtering' as test_phase;

-- 仅在第一个时间段内查询最近的点
SELECT COUNT(*) as knn_time_range_1
FROM trace_knn_query(116.3978, 39.9046, 10.9, 5, 1000.0, 1500.0, 7);

-- 仅在第二个时间段内查询最近的点
SELECT COUNT(*) as knn_time_range_2
FROM trace_knn_query(116.4005, 39.9005, 15.5, 5, 2000.0, 2500.0, 7);

-- 仅在第三个时间段内查询最近的点  
SELECT COUNT(*) as knn_time_range_3
FROM trace_knn_query(116.3905, 39.9105, 5.5, 5, 3000.0, 3500.0, 7);

-- 测试5: 数据类型过滤测试
SELECT 'Testing kNN with data type filtering' as test_phase;

-- 仅查询特定数据类型的最近点
SELECT COUNT(*) as knn_type_1
FROM trace_knn_query(116.3978, 39.9046, 10.9, 5, '-infinity'::REAL, 'infinity'::REAL, 1);

-- 测试6: 不同查询中心点测试
SELECT 'Testing different query center points' as test_phase;

-- 从数据集1的范围内查询
SELECT 
    'Dataset 1 center' as query_center,
    COUNT(*) as point_count
FROM trace_knn_query(116.3978, 39.9046, 10.9, 3, '-infinity'::REAL, 'infinity'::REAL, 7);

-- 从数据集2的范围内查询
SELECT 
    'Dataset 2 center' as query_center,
    COUNT(*) as point_count
FROM trace_knn_query(116.4005, 39.9005, 15.5, 3, '-infinity'::REAL, 'infinity'::REAL, 7);

-- 从数据集3的范围内查询
SELECT 
    'Dataset 3 center' as query_center,
    COUNT(*) as point_count
FROM trace_knn_query(116.3905, 39.9105, 5.5, 3, '-infinity'::REAL, 'infinity'::REAL, 7);

-- 测试7: 距离验证测试
SELECT 'Testing distance calculations' as test_phase;

-- 查询最近的点并验证距离递增
WITH knn_results AS (
    SELECT 
        distance,
        ROW_NUMBER() OVER (ORDER BY distance) as rn
    FROM trace_knn_query(116.3978, 39.9046, 10.9, 5, '-infinity'::REAL, 'infinity'::REAL, 7)
)
SELECT 
    COUNT(*) as total_results,
    MIN(distance) as min_distance,
    MAX(distance) as max_distance,
    COUNT(CASE WHEN rn > 1 AND distance >= LAG(distance) OVER (ORDER BY rn) THEN 1 END) as correctly_ordered
FROM knn_results;

-- 测试8: 边界情况测试
SELECT 'Testing boundary cases' as test_phase;

-- k=0 (应该返回空结果)
SELECT COUNT(*) as knn_k_zero
FROM trace_knn_query(116.3978, 39.9046, 10.9, 0, '-infinity'::REAL, 'infinity'::REAL, 7);

-- 查询中心在数据范围外
SELECT COUNT(*) as knn_outside_range
FROM trace_knn_query(200.0, 200.0, 200.0, 5, '-infinity'::REAL, 'infinity'::REAL, 7);

-- 空的时间范围
SELECT COUNT(*) as knn_empty_time
FROM trace_knn_query(116.3978, 39.9046, 10.9, 5, 5000.0, 6000.0, 7);

-- 测试9: 具体结果验证
SELECT 'Testing specific result validation' as test_phase;

-- 查询最近的点并显示详细信息
SELECT 
    ROUND(distance::numeric, 6) as rounded_distance,
    (point).x, (point).y, (point).z,
    (point).time_stamp,
    (point).file_id, (point).point_id
FROM trace_knn_query(116.3974, 39.9042, 10.5, 3, '-infinity'::REAL, 'infinity'::REAL, 7)
ORDER BY distance;

-- 测试10: 性能和统计
SELECT 'Performance and statistics' as test_phase;


-- 验证查询是否使用了索引（通过检查索引表是否有数据）
SELECT 
    'Index usage verification' as info,
    (SELECT COUNT(*) FROM tsdmp_octree_leaf) as octree_leaves,
    (SELECT COUNT(*) FROM tsdmp_leaf_bucket) as leaf_buckets,
    (SELECT COUNT(*) FROM tsdmp_bucket_kdleaf) as kd_leaves;

-- 测试完成
SELECT 'kNN query tests completed successfully' as final_status;