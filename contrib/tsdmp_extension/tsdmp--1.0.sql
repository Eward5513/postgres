-- TSDMP Extension SQL Definition File
-- Version 1.0

-- 创建自定义数据类型

-- 时空数据点类型
CREATE TYPE spatiotemporal_point AS (
    x REAL,
    y REAL,
    z REAL,
    time_stamp REAL,
    data_type SMALLINT,
    file_id INTEGER,
    point_id INTEGER,
    user_id SMALLINT,
    intensity REAL,
    speed REAL,
    color_r SMALLINT,
    color_g SMALLINT,
    color_b SMALLINT
);

-- 空间边界类型
CREATE TYPE spatial_bounds AS (
    min_x REAL,
    min_y REAL,
    min_z REAL,
    max_x REAL,
    max_y REAL,
    max_z REAL,
    min_time REAL,
    max_time REAL
);

-- 加载结果类型
CREATE TYPE load_result AS (
    files_loaded INTEGER,
    total_points BIGINT,
    global_bounds spatial_bounds,
    load_time_seconds REAL
);

-- 索引构建结果类型
CREATE TYPE index_result AS (
    index_build_time REAL,
    chunk_count INTEGER,
    total_octree_nodes INTEGER,
    total_kdtree_nodes INTEGER
);

-- 查询结果类型
CREATE TYPE query_result AS (
    points spatiotemporal_point[],
    result_count INTEGER,
    query_time_ms REAL
);

-- kNN查询结果类型
CREATE TYPE knn_result AS (
    distance REAL,
    point spatiotemporal_point
);

-- 创建存储表

-- 原始数据表
CREATE TABLE IF NOT EXISTS tsdmp_raw_data (
    id SERIAL PRIMARY KEY,
    file_path TEXT NOT NULL UNIQUE,
    file_type VARCHAR(10),
    data_points BYTEA,
    point_count BIGINT,
    bounds spatial_bounds,
    user_id SMALLINT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 索引结构表 - 八叉树节点
CREATE TABLE IF NOT EXISTS tsdmp_octree_nodes (
    chunk_id INTEGER,
    node_id INTEGER,
    parent_id INTEGER,
    level INTEGER,
    bounds spatial_bounds,
    is_leaf BOOLEAN,
    point_count BIGINT,
    children INTEGER[8],
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (chunk_id, node_id)
);

-- 索引结构表 - KD树节点
CREATE TABLE IF NOT EXISTS tsdmp_kdtree_nodes (
    chunk_id INTEGER,
    octree_node_id INTEGER,
    kd_node_id INTEGER,
    point spatiotemporal_point,
    left_child INTEGER,
    right_child INTEGER,
    division_axis SMALLINT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (chunk_id, octree_node_id, kd_node_id)
);

-- 系统配置表
CREATE TABLE IF NOT EXISTS tsdmp_config (
    key VARCHAR(100) PRIMARY KEY,
    value TEXT,
    description TEXT,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 插入默认配置
INSERT INTO tsdmp_config (key, value, description) VALUES
('chunk_max_level', '6', 'Maximum level for chunk subdivision'),
('octree_max_level', '12', 'Maximum level for octree nodes'),
('max_point_per_leaf', '400', 'Maximum points per leaf node'),
('thread_pool_size', '32', 'Thread pool size for parallel processing'),
('sample_ratio', '0.1', 'Sampling ratio for index construction')
ON CONFLICT (key) DO NOTHING;

-- 创建索引
CREATE INDEX IF NOT EXISTS idx_tsdmp_raw_data_file_type ON tsdmp_raw_data(file_type);
CREATE INDEX IF NOT EXISTS idx_tsdmp_raw_data_user_id ON tsdmp_raw_data(user_id);
CREATE INDEX IF NOT EXISTS idx_tsdmp_octree_nodes_level ON tsdmp_octree_nodes(level);
CREATE INDEX IF NOT EXISTS idx_tsdmp_octree_nodes_is_leaf ON tsdmp_octree_nodes(is_leaf);

-- 函数声明

-- 数据加载函数
CREATE OR REPLACE FUNCTION tsdmp_load_data(
    source_directory TEXT,
    max_file_num INTEGER DEFAULT 200,
    sample_ratio REAL DEFAULT 0.1
) 
RETURNS load_result
AS 'MODULE_PATHNAME', 'tsdmp_load_data'
LANGUAGE C STRICT;

-- 索引构建函数
CREATE OR REPLACE FUNCTION tsdmp_build_index(
    chunk_max_level INTEGER DEFAULT 6,
    octree_max_level INTEGER DEFAULT 12,
    max_point_per_leaf INTEGER DEFAULT 400
)
RETURNS index_result
AS 'MODULE_PATHNAME', 'tsdmp_build_index'
LANGUAGE C STRICT;

-- 范围查询函数
CREATE OR REPLACE FUNCTION tsdmp_range_query(
    min_x REAL, min_y REAL, min_z REAL,
    max_x REAL, max_y REAL, max_z REAL,
    min_time REAL, max_time REAL,
    data_type_mask INTEGER DEFAULT 7
)
RETURNS SETOF spatiotemporal_point
AS 'MODULE_PATHNAME', 'tsdmp_range_query'
LANGUAGE C STRICT;

-- kNN查询函数
CREATE OR REPLACE FUNCTION tsdmp_knn_query(
    center_x REAL, center_y REAL, center_z REAL,
    k INTEGER,
    min_time REAL DEFAULT '-infinity'::REAL,
    max_time REAL DEFAULT 'infinity'::REAL,
    data_type_mask INTEGER DEFAULT 7
)
RETURNS SETOF knn_result
AS 'MODULE_PATHNAME', 'tsdmp_knn_query'
LANGUAGE C STRICT;

-- 清理函数
CREATE OR REPLACE FUNCTION tsdmp_clear_data()
RETURNS BOOLEAN
AS 'MODULE_PATHNAME', 'tsdmp_clear_data'
LANGUAGE C STRICT;

-- 统计信息函数
CREATE OR REPLACE FUNCTION tsdmp_get_stats()
RETURNS TABLE(
    total_files INTEGER,
    total_points BIGINT,
    total_chunks INTEGER,
    index_size_mb REAL
)
AS 'MODULE_PATHNAME', 'tsdmp_get_stats'
LANGUAGE C STRICT;

-- 配置管理函数
CREATE OR REPLACE FUNCTION tsdmp_set_config(
    config_key TEXT,
    config_value TEXT
)
RETURNS BOOLEAN
AS 'MODULE_PATHNAME', 'tsdmp_set_config'
LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION tsdmp_get_config(
    config_key TEXT
)
RETURNS TEXT
AS 'MODULE_PATHNAME', 'tsdmp_get_config'
LANGUAGE C STRICT; 