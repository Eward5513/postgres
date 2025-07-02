# TSDMP PostgreSQL Extension

TSDMP (Time-Spatial Data Management Platform) PostgreSQL Extension provides efficient spatiotemporal data management capabilities.

## Build System

This extension uses PostgreSQL's PGXS (PostgreSQL Extension Building Infrastructure) by default for building and installation.

### Default Build (PGXS)
```bash
# Check dependencies
./build.sh --check-deps

# Build and install
./build.sh

# Build only (no install)
./build.sh --build-only
```

### Alternative Build Methods

If you need to use the traditional PostgreSQL source tree build method, you can disable PGXS:

```bash
# Disable PGXS and use traditional build
make NO_PGXS=1 all
make NO_PGXS=1 install
```

## Dependencies

- PostgreSQL development headers (postgresql-server-dev-XX)
- G++ compiler with C++17 support
- Boost libraries (system, filesystem, thread, asio)
- libpqxx (PostgreSQL C++ library)
- make utility

## Installation

1. Check dependencies:
   ```bash
   ./build.sh --check-deps
   ```

2. Build and install:
   ```bash
   ./build.sh
   ```

3. Create extension in your database:
   ```sql
   CREATE EXTENSION tsdmp;
   ```

## Usage

After installation, you can use TSDMP functions for spatiotemporal data management:

```sql
-- Load data from files
SELECT tsdmp_load_data(ARRAY['/path/to/data']);

-- Build spatial index
SELECT tsdmp_build_index();

-- Perform range queries
SELECT * FROM tsdmp_range_query(min_x, min_y, min_z, max_x, max_y, max_z, min_time, max_time);

-- Perform kNN queries
SELECT * FROM tsdmp_knn_query(query_x, query_y, query_z, k);
```

## Build Options

The build script supports several options:

- `--check-deps`: Only check dependencies
- `--clean-only`: Only clean previous build
- `--build-only`: Build without installing
- `--no-clean`: Don't clean before building
- `--test-db DB`: Test extension in specified database
- `--help`: Show help message

## Troubleshooting

If you encounter build issues:

1. Run dependency check: `./build.sh --check-deps`
2. Install missing dependencies as suggested
3. Try building again: `./build.sh`

For Ubuntu/Debian:
```bash
sudo apt-get install postgresql-server-dev-all build-essential libboost-all-dev libpqxx-dev
```

For CentOS/RHEL:
```bash
sudo yum install postgresql-devel gcc-c++ boost-devel libpqxx-devel
```

## 重要说明

**本扩展直接使用原始TSDMP项目的核心实现**，而不是重新实现功能。这确保了：

1. **完全兼容性**：与原始TSDMP项目的算法和数据结构完全一致
2. **性能保证**：保持原始项目的高性能特性
3. **功能完整性**：包含所有原始项目的高级功能
4. **维护简便性**：原始项目的更新可以直接应用

## 架构设计

### 核心组件

1. **原始TSDMP核心** (`../TSDMP/src/`)
   - `parameter.h/cpp` - 配置参数管理
   - `DSTdata.h/cpp` - 核心数据结构
   - `OutOfCore.h/cpp` - 外存管理和数据处理
   - `OctreeNode.h/cpp` - 八叉树节点实现
   - `Oktree-Index.h/cpp` - 八叉树索引
   - `DB_structure.h/cpp` - 数据库结构管理
   - `Range.h/cpp` - 范围查询实现
   - `kNN.h/cpp` - k近邻查询实现
   - `query.h/cpp` - 查询管理器
   - `Trajectory.h/cpp` - 轨迹数据处理

2. **PostgreSQL桥接层** (`src/`)
   - `tsdmp_main.c` - PostgreSQL扩展主入口
   - `tsdmp_bridge.cpp` - 原始TSDMP与PostgreSQL的桥接函数

3. **头文件** (`include/`)
   - `tsdmp.h` - 扩展头文件，引用原始TSDMP头文件

## 功能特性

### 数据加载
- **多格式支持**：PLY点云、CSV轨迹、OBJ网格文件
- **批量处理**：支持目录扫描和批量文件加载
- **采样控制**：可配置的数据采样比例
- **边界计算**：自动计算全局空间-时间边界

### 索引构建
- **分层索引**：Chunk → Octree → KD-tree 三层索引结构
- **并行构建**：多线程并行索引构建
- **参数可调**：可配置的分块级别、八叉树深度、叶节点容量

### 查询功能
- **范围查询**：空间-时间范围查询
- **k近邻查询**：基于距离的k近邻搜索
- **数据类型过滤**：支持点云、网格、轨迹数据类型过滤
- **高性能**：利用分层索引实现高效查询

## 配置管理

### 查看配置
```sql
SELECT tsdmp_get_config('chunk_max_level');
SELECT tsdmp_get_config('octree_max_depth');
SELECT tsdmp_get_config('max_point_per_leaf');
```

### 修改配置
```sql
SELECT tsdmp_set_config('chunk_max_level', '8');
SELECT tsdmp_set_config('octree_max_depth', '15');
```

## 数据类型

### 空间-时间点
```sql
-- spatiotemporal_point 复合类型
-- (x, y, z, time, data_type, file_id, point_id, user_id, intensity, speed, color_r, color_g, color_b)
```

### 空间边界
```sql
-- spatial_bounds 复合类型
-- (min_x, min_y, min_z, max_x, max_y, max_z, min_time, max_time)
```

### 查询结果
```sql
-- load_result: (files_loaded, total_points, global_bounds, load_time_seconds)
-- index_result: (index_build_time, chunk_count, total_octree_nodes, total_kdtree_nodes)
-- knn_result: (distance, point)
```

## 性能优化

### 内存管理
- 使用PostgreSQL内存上下文管理
- 查询时使用独立内存上下文
- 自动内存清理和垃圾回收

### 并行处理
- 数据加载并行处理
- 索引构建多线程优化
- 查询并行执行

### 存储优化
- 分层存储结构
- 高效的空间索引
- 压缩存储支持

## 故障排除

### 编译问题
1. **找不到Boost库**：检查BOOST_ROOT路径设置
2. **PostgreSQL头文件缺失**：安装postgresql-server-dev
3. **C++17支持**：确保编译器支持C++17标准

### 运行时问题
1. **扩展加载失败**：检查库文件权限和路径
2. **内存不足**：调整PostgreSQL内存配置
3. **查询超时**：优化查询参数或增加索引深度

## 开发说明

### 桥接函数
桥接函数位于 `src/tsdmp_bridge.cpp`，负责：
- 将PostgreSQL参数转换为原始TSDMP格式
- 调用原始TSDMP核心函数
- 将结果转换为PostgreSQL格式

### 扩展原始功能
要添加新功能：
1. 在原始TSDMP项目中实现核心逻辑
2. 在桥接层添加包装函数
3. 在主文件中添加PostgreSQL函数接口
4. 更新SQL定义文件

## 许可证

本扩展遵循原始TSDMP项目的许可证条款。

## 贡献

欢迎提交问题报告和功能请求。在修改核心算法时，请确保与原始TSDMP项目保持同步。 