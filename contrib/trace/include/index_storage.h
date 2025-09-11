#ifndef INDEX_STORAGE_H
#define INDEX_STORAGE_H

#include <string>
#include <vector>
#include <queue>
#include <cstdint>

#include "trace.h" // for SimpleBounds, SimplePoint

// 叶子元数据（来自 DB）
struct LeafMeta {
    std::string file_path;
    uint32_t point_count;
    float minx, miny, minz, maxx, maxy, maxz;
};

struct KnnCand { 
    float dist2; 
    float x,y,z; 
    uint32_t fid,row; 
    
    // Comparison operator for STL containers
    bool operator<(const KnnCand& other) const {
        return dist2 < other.dist2;
    }
};

// SQL 字面量转义（对单引号翻倍）
std::string sql_quote_literal(const std::string &s);

// 构建阶段使用的点结构
struct BuildPoint { float x,y,z; uint32_t fid,row; };

// 构建前准备（建表、清理旧数据、创建输出目录）
void persist_prepare_dataset(const std::string &dataset_path);

// 持久化单个叶子：写叶子文件，写三层索引（叶子、叶内octree桶自适应、桶内kd叶）到数据库
int persist_leaf_index(const std::string &dataset_path,
                        uint64_t leaf_prefix, int leaf_level,
                        uint32_t xi_cell, uint32_t yi_cell, uint32_t zi_cell,
                        float minx, float miny, float minz, float maxx, float maxy, float maxz,
                        const std::vector<BuildPoint> &points,
                        int bucket_max_points, int max_inner_levels, int kd_leaf_max_points,
                        int &out_bucket_count);

// 叶子 bbox 到点的最小距离平方
float bbox_point_min_dist2(const LeafMeta &m, float x, float y, float z);

// 从叶子文件读取点并按边界过滤，追加到 out
void read_leaf_points_filter_bounds(const LeafMeta &m, const SimpleBounds &b, int data_type_mask, std::vector<SimplePoint> &out);

// 从叶子文件读取点并更新 kNN 最大堆（dist2, cand）
void read_leaf_points_update_knn(const LeafMeta &m, const SimplePoint &c, int k, std::priority_queue<std::pair<float,KnnCand>> &heap);

// 桶级元数据
struct BucketMeta {
    std::string file_path;
    int bx,by,bz; int level; // level = inner_octree_levels
    uint64_t leaf_prefix; int leaf_level;
    uint64_t offset; uint32_t count;
    float minx, miny, minz, maxx, maxy, maxz;
};

// kd 叶子元数据
struct KdLeafMeta {
    std::string file_path;
    int bx,by,bz; uint64_t leaf_prefix; int leaf_level;
    uint64_t offset; uint32_t count;
    float minx, miny, minz, maxx, maxy, maxz;
};

// 按文件偏移读取 count 条数据并做边界过滤
void read_points_block_filter_bounds(const std::string &file_path, uint64_t offset, uint32_t count,
                                     const SimpleBounds &b, int data_type_mask,
                                     std::vector<SimplePoint> &out);

// 按文件偏移读取 count 条数据并更新 kNN 堆
void read_points_block_update_knn(const std::string &file_path, uint64_t offset, uint32_t count,
                                  const SimplePoint &c, int k,
                                  std::priority_queue<std::pair<float,KnnCand>> &heap);

// 数据库查询函数
std::vector<BucketMeta> db_query_buckets_intersecting(const std::string &dataset_path,
                                                      const SimpleBounds &b);

std::vector<KdLeafMeta> db_query_all_kdleaves(const std::string &dataset_path);

#endif // INDEX_STORAGE_H


