--
-- trace_basic.sql
-- 基本数据加载和配置测试
--

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

-- 加载trace扩展
CREATE EXTENSION IF NOT EXISTS trace;

-- 测试1: 检查初始状态
SELECT 'Testing initial state' as test_phase;

-- 测试扩展是否正确加载
SELECT COUNT(*) > 0 as extension_loaded 
FROM pg_extension WHERE extname = 'trace';

-- 检查索引表是否为空
SELECT COUNT(*) as octree_leaf_count FROM tsdmp_octree_leaf;
SELECT COUNT(*) as leaf_bucket_count FROM tsdmp_leaf_bucket; 
SELECT COUNT(*) as bucket_kdleaf_count FROM tsdmp_bucket_kdleaf;

-- 测试2: 基本配置测试
SELECT 'Testing configuration functions' as test_phase;


-- 测试3: 数据加载测试
SELECT 'Testing data loading' as test_phase;


-- 加载测试数据
\set datadir :abs_srcdir '/data'
SELECT trace_load_data(:'datadir', 10, 1.0) as load_result;

-- 验证加载结果
SELECT 
    (load_result).files_loaded as files_loaded,
    (load_result).total_points as total_points,
    (load_result).load_time_seconds > 0 as has_load_time
FROM (
    SELECT trace_load_data(:'datadir', 3, 1.0) as load_result
) t;

-- 验证边界信息
SELECT 
    (load_result).min_x,
    (load_result).max_x,
    (load_result).min_y, 
    (load_result).max_y,
    (load_result).min_z,
    (load_result).max_z
FROM (
    SELECT trace_load_data(:'datadir', 3, 1.0) as load_result
) t;

-- 测试4: 统计信息测试
SELECT 'Testing statistics functions' as test_phase;


-- 测试5: 参数测试
SELECT 'Testing parameter variations' as test_phase;

-- 测试不同文件数量限制
SELECT 
    (trace_load_data(:'datadir', 1, 1.0)).files_loaded as files_with_limit_1;

SELECT 
    (trace_load_data(:'datadir', 2, 1.0)).files_loaded as files_with_limit_2;

-- 测试不同采样率
SELECT 
    (trace_load_data(:'datadir', 3, 0.5)).total_points as points_with_sampling;

-- 测试6: 错误处理测试
SELECT 'Testing error handling' as test_phase;

-- 测试不存在的目录（应该产生错误）
-- SELECT trace_load_data('/nonexistent/path', 10, 1.0) as error_test;

-- 测试完成
SELECT 'Basic tests completed successfully' as final_status;
