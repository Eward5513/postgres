--
-- trace_range_query.sql
-- 范围查询测试
--

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

-- 加载trace扩展
SET client_min_messages TO WARNING;
CREATE EXTENSION IF NOT EXISTS trace;
SET client_min_messages TO NOTICE;

-- 测试1: 准备数据和索引
SELECT 'Preparing data and index for range queries' as test_phase;


-- 加载测试数据（不测试加载时间）
\set datadir :abs_srcdir '/data'
SELECT 
    (result).files_loaded as files_loaded,
    (result).total_points as total_points
FROM (
    SELECT trace_load_data(:'datadir', 3, 1.0) as result
) t;

-- 设置GUC参数以获得更细粒度的树结构
SET trace.max_point_per_leaf = 10;
SET trace.bucket_max_points = 4;
SET trace.kd_leaf_max_points = 2;

-- 构建索引（不测试构建时间）
SELECT 
    (result).chunk_count as chunk_count,
    (result).total_octree_nodes as octree_nodes,
    (result).total_kdtree_nodes as kdtree_nodes
FROM (
    SELECT trace_build_index() as result
) t;

-- 测试2: 基本范围查询
SELECT 'Testing basic range queries' as test_phase;

-- 查询整个数据范围
SELECT COUNT(*) as total_points_in_full_range
FROM trace_range_query(116.39, 39.90, 5.0, 116.41, 39.92, 16.0, 7);

-- 查询小范围
SELECT COUNT(*) as points_in_small_range
FROM trace_range_query(116.397, 39.904, 10.0, 116.398, 39.905, 11.0, 7);

-- 查询特定数据类型
SELECT COUNT(*) as points_with_type_1
FROM trace_range_query(116.39, 39.90, 5.0, 116.41, 39.92, 16.0, 1);

-- 测试3: 边界测试
SELECT 'Testing range query boundaries' as test_phase;

-- 测试边界上的点
SELECT COUNT(*) as boundary_points
FROM trace_range_query(116.3974, 39.9042, 10.5, 116.3974, 39.9042, 10.5, 7);

-- 测试稍微扩展的边界
SELECT COUNT(*) as expanded_boundary_points
FROM trace_range_query(116.3973, 39.9041, 10.4, 116.3975, 39.9043, 10.6, 7);

-- 测试4: 数据类型过滤测试
SELECT 'Testing data type filtering' as test_phase;

-- 查询所有数据类型
SELECT COUNT(*) as all_data_types
FROM trace_range_query(116.39, 39.90, 5.0, 116.41, 39.92, 16.0, 7);

-- 查询特定数据类型
SELECT COUNT(*) as specific_data_type
FROM trace_range_query(116.39, 39.90, 5.0, 116.41, 39.92, 16.0, 1);

-- 测试5: 具体点数据检查
SELECT 'Testing specific point data' as test_phase;

-- 查询并显示部分具体点数据
SELECT 
    x, y, z
FROM trace_range_query(116.397, 39.904, 10.0, 116.398, 39.905, 11.0, 7)
ORDER BY x, y, z
LIMIT 5;

-- 测试6: 空结果测试
SELECT 'Testing empty result scenarios' as test_phase;

-- 查询不存在的空间范围
SELECT COUNT(*) as empty_spatial_range
FROM trace_range_query(200.0, 200.0, 200.0, 201.0, 201.0, 201.0, 7);

-- 查询不存在的数据类型
SELECT COUNT(*) as empty_data_type
FROM trace_range_query(116.39, 39.90, 5.0, 116.41, 39.92, 16.0, 4);

-- 测试7: 参数边界测试
SELECT 'Testing parameter boundaries' as test_phase;

-- 测试最小范围
SELECT COUNT(*) as minimal_range
FROM trace_range_query(116.3974, 39.9042, 10.5, 116.3974, 39.9042, 10.5, 7);

-- 测试反向边界（min > max，应该返回错误）
\set ON_ERROR_STOP off
SELECT COUNT(*) as reverse_spatial_bounds
FROM trace_range_query(116.398, 39.905, 11.0, 116.397, 39.904, 10.0, 7);

-- time bounds removed, skip reverse_time_bounds test
\set ON_ERROR_STOP on

-- 测试8: 性能相关统计
SELECT 'Performance and statistics' as test_phase;


-- 测试完成
SELECT 'Range query tests completed successfully' as final_status;