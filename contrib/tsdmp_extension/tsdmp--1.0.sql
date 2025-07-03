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

-- 加载结果类型
CREATE TYPE load_result AS (
    files_loaded INTEGER,
    total_points BIGINT,
    load_time_seconds REAL
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

-- 创建存储表

-- 八叉树节点存储表 (用于序列化的二进制数据存储)
CREATE TABLE IF NOT EXISTS all_octree_table (
    key INT PRIMARY KEY,
    data BYTEA,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- KD树节点存储表 (用于序列化的二进制数据存储)
CREATE TABLE IF NOT EXISTS all_kdtree (
    key1 INT,
    key2 INT,
    data BYTEA,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (key1, key2)
);

-- 网格连接存储表 (用于存储二进制格式的连接数据)
CREATE TABLE IF NOT EXISTS mesh_connections_table (
    key INT PRIMARY KEY,
    data BYTEA
);

-- 用户数据存储表 (用于存储大对象引用)
CREATE TABLE IF NOT EXISTS user_data (
    key INT PRIMARY KEY,
    lo_oid OID,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

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