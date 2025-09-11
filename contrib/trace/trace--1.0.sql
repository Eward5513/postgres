-- TRACE Extension SQL Definition File
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
    speed REAL
);

-- 加载结果类型（时间字段已移除）
CREATE TYPE load_result AS (
    files_loaded INTEGER,
    total_points BIGINT,
    load_time_seconds REAL,
    -- 边界信息（空间边界，不包括时间）
    min_x REAL,
    max_x REAL,
    min_y REAL,
    max_y REAL,
    min_z REAL,
    max_z REAL,
    -- 统计信息
    total_file_size_bytes BIGINT,
    avg_points_per_file REAL,
    dataset_path TEXT
);

-- 索引构建结果类型
CREATE TYPE index_result AS (
    index_build_time REAL,
    chunk_count INTEGER,
    total_octree_nodes INTEGER,
    total_kdtree_nodes INTEGER
);

-- kNN查询结果类型
CREATE TYPE knn_result AS (
    distance REAL,
    point spatiotemporal_point
);

-- 创建存储表（仅保留当前实现使用到的表）

-- 数据集信息表（单行表）
CREATE TABLE IF NOT EXISTS trace_dataset_info (
    dataset_path TEXT NOT NULL,
    total_files INTEGER NOT NULL,
    total_points BIGINT NOT NULL,
    sample_ratio REAL NOT NULL,
    min_x REAL NOT NULL,
    max_x REAL NOT NULL,
    min_y REAL NOT NULL,
    max_y REAL NOT NULL,
    min_z REAL NOT NULL,
    max_z REAL NOT NULL,
    load_duration_seconds REAL NOT NULL,
    file_paths TEXT, -- JSON array of file paths
    last_load_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 外层叶子索引（文件块归属）
CREATE TABLE IF NOT EXISTS trace_octree_leaf (
    dataset_path text NOT NULL,
    prefix bigint NOT NULL,
    level int NOT NULL,
    xi int NOT NULL,
    yi int NOT NULL,
    zi int NOT NULL,
    point_count int NOT NULL,
    file_path text NOT NULL,
    minx real NOT NULL, miny real NOT NULL, minz real NOT NULL,
    maxx real NOT NULL, maxy real NOT NULL, maxz real NOT NULL,
    PRIMARY KEY(dataset_path, prefix, level)
);

-- 叶内自适应 octree 桶
CREATE TABLE IF NOT EXISTS trace_leaf_bucket (
    dataset_path text NOT NULL,
    leaf_prefix bigint NOT NULL,
    leaf_level int NOT NULL,
    bx int NOT NULL, by int NOT NULL, bz int NOT NULL,
    level int NOT NULL,
    data_offset bigint NOT NULL,
    count int NOT NULL,
    file_path text NOT NULL,
    minx real NOT NULL, miny real NOT NULL, minz real NOT NULL,
    maxx real NOT NULL, maxy real NOT NULL, maxz real NOT NULL,
    PRIMARY KEY(dataset_path, leaf_prefix, leaf_level, bx, by, bz, level)
);

-- 桶内 kd 叶（块偏移）
CREATE TABLE IF NOT EXISTS trace_bucket_kdleaf (
    dataset_path text NOT NULL,
    leaf_prefix bigint NOT NULL,
    leaf_level int NOT NULL,
    bx int NOT NULL, by int NOT NULL, bz int NOT NULL,
    kd_idx int NOT NULL,
    data_offset bigint NOT NULL,
    count int NOT NULL,
    file_path text NOT NULL,
    minx real NOT NULL, miny real NOT NULL, minz real NOT NULL,
    maxx real NOT NULL, maxy real NOT NULL, maxz real NOT NULL,
    PRIMARY KEY(dataset_path, leaf_prefix, leaf_level, bx, by, bz, kd_idx)
);

-- 函数声明

-- 数据加载函数
CREATE OR REPLACE FUNCTION trace_load_data(
    source_directory TEXT,
    max_file_num INTEGER DEFAULT 200,
    sample_ratio REAL DEFAULT 1.0
) 
RETURNS load_result
AS 'MODULE_PATHNAME', 'trace_load_data'
LANGUAGE C STRICT;

-- 索引构建函数
-- 使用 GUC 变量进行配置:
-- SET trace.chunk_max_level = 8;
-- SET trace.octree_max_level = 15;
-- SET trace.max_point_per_leaf = 2000;
CREATE OR REPLACE FUNCTION trace_build_index()
RETURNS index_result
AS 'MODULE_PATHNAME', 'trace_build_index'
LANGUAGE C STRICT;

-- 范围查询函数
CREATE OR REPLACE FUNCTION trace_range_query(
    min_x REAL, min_y REAL, min_z REAL,
    max_x REAL, max_y REAL, max_z REAL,
    min_time REAL, max_time REAL,
    data_type_mask INTEGER DEFAULT 7
)
RETURNS SETOF spatiotemporal_point
AS 'MODULE_PATHNAME', 'trace_range_query'
LANGUAGE C STRICT;

-- kNN查询函数
CREATE OR REPLACE FUNCTION trace_knn_query(
    center_x REAL, center_y REAL, center_z REAL,
    k INTEGER,
    min_time REAL DEFAULT '-infinity'::REAL,
    max_time REAL DEFAULT 'infinity'::REAL,
    data_type_mask INTEGER DEFAULT 7
)
RETURNS SETOF knn_result
AS 'MODULE_PATHNAME', 'trace_knn_query'
LANGUAGE C STRICT;

