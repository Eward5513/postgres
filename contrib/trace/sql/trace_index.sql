--
-- trace_index.sql
-- 索引构建测试
--

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

-- 加载trace扩展
CREATE EXTENSION IF NOT EXISTS trace;

-- 测试1: 准备数据
SELECT 'Preparing data for index building' as test_phase;


-- 加载测试数据（不测试加载时间）
\set datadir :abs_srcdir '/data'
SELECT 
    (result).files_loaded as files_loaded,
    (result).total_points as total_points
FROM (
    SELECT trace_load_data(:'datadir', 3, 1.0) as result
) t;

-- 测试2: 索引构建测试
SELECT 'Testing index building' as test_phase;

-- 构建索引（不测试构建时间）
SELECT 
    (result).chunk_count as chunk_count,
    (result).total_octree_nodes as octree_nodes,
    (result).total_kdtree_nodes as kdtree_nodes
FROM (
    SELECT trace_build_index() as result
) t;

-- 测试3: 验证索引表数据
SELECT 'Verifying index table data' as test_phase;

-- 检查octree叶子表
SELECT COUNT(*) as octree_leaf_count FROM trace_octree_leaf;
SELECT dataset_path, COUNT(*) as leaf_count 
FROM trace_octree_leaf 
GROUP BY dataset_path;

-- 检查叶内桶表
SELECT COUNT(*) as leaf_bucket_count FROM trace_leaf_bucket;
SELECT dataset_path, COUNT(*) as bucket_count 
FROM trace_leaf_bucket 
GROUP BY dataset_path;

-- 检查桶内kd叶表
SELECT COUNT(*) as bucket_kdleaf_count FROM trace_bucket_kdleaf;
SELECT dataset_path, COUNT(*) as kdleaf_count 
FROM trace_bucket_kdleaf 
GROUP BY dataset_path;

-- 测试4: 索引完整性检查
SELECT 'Testing index integrity' as test_phase;

-- 检查所有叶子的边界框是否合理
SELECT 
    COUNT(*) as total_leaves,
    COUNT(CASE WHEN minx <= maxx AND miny <= maxy AND minz <= maxz THEN 1 END) as valid_bounds
FROM trace_octree_leaf;

-- 检查桶的层级结构
SELECT 
    level, 
    COUNT(*) as bucket_count,
    MIN(count) as min_points,
    MAX(count) as max_points,
    AVG(count) as avg_points
FROM trace_leaf_bucket
GROUP BY level
ORDER BY level;

-- 测试5: 多次构建测试
SELECT 'Testing multiple index builds' as test_phase;

-- 再次构建索引（应该清理并重建，不测试构建时间）
SELECT 
    (result).chunk_count as chunk_count,
    (result).total_octree_nodes as octree_nodes,
    (result).total_kdtree_nodes as kdtree_nodes
FROM (
    SELECT trace_build_index() as result
) t;

-- 验证重建后的数据一致性
SELECT 
    (SELECT COUNT(*) FROM trace_octree_leaf) as octree_count,
    (SELECT COUNT(*) FROM trace_leaf_bucket) as bucket_count,
    (SELECT COUNT(*) FROM trace_bucket_kdleaf) as kdleaf_count;

-- 测试完成
SELECT 'Index building tests completed successfully' as final_status;
