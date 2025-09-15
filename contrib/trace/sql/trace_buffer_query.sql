--
-- trace_buffer_query.sql
-- 缓冲区查询测试
--

\getenv abs_srcdir PG_ABS_SRCDIR

SET client_min_messages TO WARNING;
CREATE EXTENSION IF NOT EXISTS trace;
SET client_min_messages TO NOTICE;

-- 准备数据与索引
SELECT 'Preparing data and index for buffer queries' as test_phase;

\set datadir :abs_srcdir '/data'
SELECT 
    (result).files_loaded as files_loaded,
    (result).total_points as total_points
FROM (
    SELECT trace_load_data(:'datadir', 3, 1.0) as result
) t;

SET trace.max_point_per_leaf = 10;
SET trace.bucket_max_points = 4;
SET trace.kd_leaf_max_points = 2;

SELECT 
    (result).chunk_count as chunk_count,
    (result).total_octree_nodes as octree_nodes,
    (result).total_kdtree_nodes as kdtree_nodes
FROM (
    SELECT trace_build_index() as result
) t;

-- 基本buffer查询
SELECT 'Testing basic buffer query' as test_phase;

-- 以数据集1中心，半径0.15
SELECT COUNT(*) as cnt
FROM trace_buffer_query(116.3978, 39.9046, 10.9, 0.15, 7);

-- 较大半径，覆盖一个数据集
SELECT COUNT(*) as cnt_large
FROM trace_buffer_query(116.3978, 39.9046, 10.9, 0.25, 7);

-- 数据类型过滤
SELECT COUNT(*) as cnt_type1
FROM trace_buffer_query(116.3978, 39.9046, 10.9, 0.25, 1);

-- 边界与异常
SELECT 'Testing boundary and invalid cases' as test_phase;
\set ON_ERROR_STOP off
SELECT COUNT(*) FROM trace_buffer_query(116.3978, 39.9046, 10.9, 0.0, 7);
\set ON_ERROR_STOP on

-- 结果检查（取前3个点）
SELECT x,y,z
FROM trace_buffer_query(116.3974, 39.9042, 10.5, 0.21, 7)
ORDER BY x,y,z
LIMIT 3;

-- 完成
SELECT 'Buffer query tests completed successfully' as final_status;


