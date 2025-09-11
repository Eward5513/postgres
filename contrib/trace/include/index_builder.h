#ifndef INDEX_BUILDER_H
#define INDEX_BUILDER_H

#include <string>
#include <vector>
#include <cstdint>

#include "trace.h" // SimpleBounds

// Forward declarations
struct BuildPoint; // from index_storage.h

class IndexBuilder {
public:
    struct Params {
        int octree_max_level;
        int max_points_per_leaf;
        int bucket_max_points;
        int max_inner_levels;
        int kd_leaf_max_points;
    };

    IndexBuilder(const std::string &dataset_path,
                 const SimpleBounds &global_bounds,
                 const std::vector<std::string> &source_files,
                 const Params &params);

    // 执行完整构建：扫描原始CSV -> Morton排序 -> 外层octree叶 -> 按叶持久化(自适应内层octree + kd叶)
    // 通过 out_result 返回统计：octree 叶数量、kd 叶数量
    void build_all(IndexResult &out_result);

private:
    std::string dataset_path_;
    SimpleBounds bounds_;
    std::vector<std::string> files_;
    Params params_;

    struct Posting {
        uint64_t morton;
        float x,y,z; uint32_t fid,row;
    };

    std::vector<Posting> postings_;

    struct Node {
        uint64_t prefix; int level; uint32_t start,end; uint32_t xi,yi,zi; bool is_leaf;
    };
    std::vector<Node> nodes_;

    // steps
    void scan_points_();
    void sort_by_morton_();
    void bulkload_octree_();
    void persist_leaves_(int &octree_leaf_count, int &kd_leaf_count);

    // helpers
    static uint64_t morton3_(uint32_t xi, uint32_t yi, uint32_t zi);
    static uint64_t prefix_(uint64_t key, int level);
    static void deinterleave_(uint64_t pref, int level, uint32_t &xi, uint32_t &yi, uint32_t &zi);
    static uint32_t q21_(float v, float vmin, float vmax);
};

#endif // INDEX_BUILDER_H
