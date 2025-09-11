--
-- trace_basic.sql
-- 基本数据加载和配置测试
--

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

-- 设置日志级别，屏蔽INFO日志以便测试对比
SET client_min_messages TO ERROR;

-- 加载trace扩展
CREATE EXTENSION IF NOT EXISTS trace;

-- 测试1: 检查初始状态
SELECT 'Testing initial state' as test_phase;

-- 测试扩展是否正确加载
SELECT COUNT(*) > 0 as extension_loaded 
FROM pg_extension WHERE extname = 'trace';

-- 检查索引表是否为空
SELECT COUNT(*) as octree_leaf_count FROM trace_octree_leaf;
SELECT COUNT(*) as leaf_bucket_count FROM trace_leaf_bucket; 
SELECT COUNT(*) as bucket_kdleaf_count FROM trace_bucket_kdleaf;

-- 测试2: 基本配置测试
SELECT 'Testing configuration functions' as test_phase;


-- 测试3: 数据加载测试
SELECT 'Testing data loading' as test_phase;


-- 加载测试数据
\set datadir :abs_srcdir '/data'
-- 分解输出，不测试加载时间
SELECT 
    (result).files_loaded as files_loaded,
    (result).total_points as total_points,
    (result).min_x as min_x,
    (result).max_x as max_x,
    (result).min_y as min_y,
    (result).max_y as max_y,
    (result).min_z as min_z,
    (result).max_z as max_z,
    (result).total_file_size_bytes as total_file_size_bytes,
    (result).avg_points_per_file as avg_points_per_file,
    (result).dataset_path as dataset_path
FROM (
    SELECT trace_load_data(:'datadir', 3, 1.0) as result
) t;

-- 验证加载结果
SELECT 
    (load_result).files_loaded as files_loaded,
    (load_result).total_points as total_points
FROM (
    SELECT trace_load_data(:'datadir', 3, 1.0) as load_result
) t;

-- 测试4: 双重数据加载测试
SELECT 'Testing double data loading' as test_phase;

-- 检查第一次加载后的数据集信息表记录数
SELECT COUNT(*) as dataset_info_count FROM trace_dataset_info;

-- 第二次加载相同数据（不测试加载时间）
SELECT 
    (result).files_loaded as files_loaded_2nd,
    (result).total_points as total_points_2nd,
    (result).min_x as min_x_2nd,
    (result).max_x as max_x_2nd,
    (result).min_y as min_y_2nd,
    (result).max_y as max_y_2nd,
    (result).min_z as min_z_2nd,
    (result).max_z as max_z_2nd,
    (result).total_file_size_bytes as total_file_size_bytes_2nd,
    (result).avg_points_per_file as avg_points_per_file_2nd,
    (result).dataset_path as dataset_path_2nd
FROM (
    SELECT trace_load_data(:'datadir', 3, 1.0) as result
) t;

-- 验证第二次加载后数据集信息表仍然只有一条记录
SELECT COUNT(*) as dataset_info_count_after_2nd FROM trace_dataset_info;

-- 验证索引表状态（应该仍然为空，因为还没有构建索引）
SELECT COUNT(*) as octree_leaf_count_after_2nd FROM trace_octree_leaf;
SELECT COUNT(*) as leaf_bucket_count_after_2nd FROM trace_leaf_bucket; 
SELECT COUNT(*) as bucket_kdleaf_count_after_2nd FROM trace_bucket_kdleaf;

-- 测试完成
SELECT 'Basic tests completed successfully' as final_status;
