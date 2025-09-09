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

-- 加载结果类型
CREATE TYPE load_result AS (
    files_loaded INTEGER,
    total_points BIGINT,
    load_time_seconds REAL,
    -- 边界信息
    min_x REAL,
    max_x REAL,
    min_y REAL,
    max_y REAL,
    min_z REAL,
    max_z REAL,
    min_time REAL,
    max_time REAL,
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

-- 原始数据存储表 (用于存储大对象引用)
CREATE TABLE IF NOT EXISTS original_data (
    key1 INT,
    key2 INT,
    lo_oid OID,
    userid_ivf_index BYTEA,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (key1, key2)
);

-- 轨迹数据存储表 (用于存储二进制格式的轨迹数据)
CREATE TABLE IF NOT EXISTS trajectory_table (
    id INT PRIMARY KEY,
    data BYTEA,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 数据集信息表 (单行存储当前加载的数据集信息)
CREATE TABLE IF NOT EXISTS trace_dataset_info (
    id INTEGER PRIMARY KEY DEFAULT 1,
    dataset_path TEXT NOT NULL,
    last_load_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    total_files INTEGER NOT NULL,
    total_points BIGINT NOT NULL,
    sample_ratio REAL NOT NULL,
    
    -- 空间边界
    min_x REAL NOT NULL,
    max_x REAL NOT NULL,
    min_y REAL NOT NULL,
    max_y REAL NOT NULL,
    min_z REAL NOT NULL,
    max_z REAL NOT NULL,
    
    -- 时间边界
    min_time REAL,
    max_time REAL,
    
    -- 加载耗时
    load_duration_seconds REAL,
    
    -- 文件路径列表（JSON格式存储）
    file_paths JSONB,
    
    -- 确保只有一行数据
    CONSTRAINT single_row CHECK (id = 1)
);

-- 点云数据存储表 (用于存储从.obj文件中提取的顶点数据)
CREATE TABLE IF NOT EXISTS point_cloud (
    id SERIAL PRIMARY KEY,
    file_id INTEGER NOT NULL,
    vertex_id INTEGER NOT NULL,
    x REAL NOT NULL,
    y REAL NOT NULL,
    z REAL NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(file_id, vertex_id)
);

-- 网格数据存储表 (用于存储从.obj文件中提取的面数据)
CREATE TABLE IF NOT EXISTS mesh (
    id SERIAL PRIMARY KEY,
    file_id INTEGER NOT NULL,
    face_id INTEGER NOT NULL,
    vertex_count INTEGER NOT NULL,
    vertex_indices INTEGER[] NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(file_id, face_id)
);

-- 为点云表创建索引
CREATE INDEX IF NOT EXISTS idx_point_cloud_spatial ON point_cloud(x, y, z);

-- 为网格表创建索引
CREATE INDEX IF NOT EXISTS idx_mesh_face_id ON mesh(face_id);

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

-- 清理函数
CREATE OR REPLACE FUNCTION trace_clear_data()
RETURNS BOOLEAN
AS 'MODULE_PATHNAME', 'trace_clear_data'
LANGUAGE C STRICT;

-- 统计信息函数
CREATE OR REPLACE FUNCTION trace_get_stats()
RETURNS TABLE(
    total_files INTEGER,
    total_points BIGINT,
    total_chunks INTEGER,
    index_size_mb REAL
)
AS 'MODULE_PATHNAME', 'trace_get_stats'
LANGUAGE C STRICT;

-- 配置管理函数
CREATE OR REPLACE FUNCTION trace_set_config(
    config_key TEXT,
    config_value TEXT
)
RETURNS BOOLEAN
AS 'MODULE_PATHNAME', 'trace_set_config'
LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION trace_get_config(
    config_key TEXT
)
RETURNS TEXT
AS 'MODULE_PATHNAME', 'trace_get_config'
LANGUAGE C STRICT;
