#include "../include/safe_header.h"

#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <queue>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <map>

#include "executor/spi.h"

#include "../include/index_storage.h"
#include "../include/geo_utils.h"
#include "../include/pgutils.h"

using std::string;
namespace fs = std::filesystem;

// Helper function to format float values for PostgreSQL
static std::string format_float_for_sql(float value) {
    if (std::isinf(value)) {
        return value > 0 ? "'infinity'" : "'-infinity'";
    } else if (std::isnan(value)) {
        return "'nan'";
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", value);
        return std::string(buf);
    }
}

// 简易 SQL 字面量转义
std::string sql_quote_literal(const std::string &s)
{
    std::string out; out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) { if (c == '\'') out.push_back('\''); out.push_back(c); }
    out.push_back('\'');
    return out;
}

// ensure_index_table_exists removed; DDL is defined in trace--1.0.sql

void persist_prepare_dataset(const std::string &dataset_path)
{
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(ERROR, "SPI_connect failed");
    }
    std::string del1 = "DELETE FROM trace_bucket_kdleaf WHERE dataset_path = " + sql_quote_literal(dataset_path);
    std::string del2 = "DELETE FROM trace_leaf_bucket WHERE dataset_path = " + sql_quote_literal(dataset_path);
    std::string del3 = "DELETE FROM trace_octree_leaf WHERE dataset_path = " + sql_quote_literal(dataset_path);
    SPI_execute(del1.c_str(), false, 0);
    SPI_execute(del2.c_str(), false, 0);
    SPI_execute(del3.c_str(), false, 0);
    SPI_finish();

    // 创建输出目录
    std::error_code ec;
    fs::path base(dataset_path);
    fs::create_directories(base / "tsdmp_index", ec);
    if (ec) {
        elog(ERROR, "Failed to create index dir: %s", (base/"tsdmp_index").string().c_str());
    }
}

static inline void update_bbox(float x, float y, float z,
                               float &minx, float &miny, float &minz,
                               float &maxx, float &maxy, float &maxz)
{
    if (x < minx) minx = x; if (x > maxx) maxx = x;
    if (y < miny) miny = y; if (y > maxy) maxy = y;
    if (z < minz) minz = z; if (z > maxz) maxz = z;
}

int persist_leaf_index(const std::string &dataset_path,
                        uint64_t leaf_prefix, int leaf_level,
                        uint32_t xi_cell, uint32_t yi_cell, uint32_t zi_cell,
                        float minx, float miny, float minz, float maxx, float maxy, float maxz,
                        const std::vector<BuildPoint> &points,
                        int bucket_max_points, int max_inner_levels, int kd_leaf_max_points,
                        int &out_bucket_count)
{
    // 插入叶子行
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(ERROR, "SPI_connect failed");
    }
    char leaf_sql[1024];
    fs::path out_dir = fs::path(dataset_path) / "tsdmp_index";
    char fname[128];
    snprintf(fname, sizeof(fname), "leaf_L%d_%016llx.bin", leaf_level, (unsigned long long)leaf_prefix);
    fs::path file_path = out_dir / std::string(fname);
    snprintf(leaf_sql, sizeof(leaf_sql),
             "INSERT INTO trace_octree_leaf(dataset_path,prefix,level,xi,yi,zi,point_count,file_path,minx,miny,minz,maxx,maxy,maxz) "
             "VALUES (%s, %lld, %d, %u, %u, %u, %u, %s, %s, %s, %s, %s, %s, %s)",
             sql_quote_literal(dataset_path).c_str(), (long long)leaf_prefix, leaf_level,
             (unsigned)xi_cell, (unsigned)yi_cell, (unsigned)zi_cell,
             (unsigned)points.size(), sql_quote_literal(file_path.string()).c_str(),
             format_float_for_sql(minx).c_str(), format_float_for_sql(miny).c_str(), format_float_for_sql(minz).c_str(),
             format_float_for_sql(maxx).c_str(), format_float_for_sql(maxy).c_str(), format_float_for_sql(maxz).c_str());
    SPI_execute(leaf_sql, false, 0);
    SPI_finish();

    // 分桶（叶内 octree）
    // 自适应叶内 octree（最大层数 max_inner_levels，桶内点数不超过 bucket_max_points）
    struct BucketKey { int bx,by,bz,level; bool operator<(const BucketKey &o) const { if (level!=o.level) return level<o.level; if (bx!=o.bx) return bx<o.bx; if (by!=o.by) return by<o.by; return bz<o.bz; } };
    struct Bucket { float bminx,bminy,bminz,bmaxx,bmaxy,bmaxz; std::vector<BuildPoint> pts; int bx,by,bz,level; };

    std::vector<Bucket> buckets;
    buckets.reserve(64);
    // 初始根桶
    buckets.push_back(Bucket{minx,miny,minz,maxx,maxy,maxz, std::vector<BuildPoint>(points.begin(), points.end()), 0,0,0, 0});

    auto subdivide = [&](const Bucket &b)->std::vector<Bucket>{
        std::vector<Bucket> out; out.reserve(8);
        float cx = 0.5f*(b.bminx + b.bmaxx), cy = 0.5f*(b.bminy + b.bmaxy), cz = 0.5f*(b.bminz + b.bmaxz);
        for (int idx=0; idx<8; ++idx) {
            int ix = (idx & 1); int iy = (idx >> 1) & 1; int iz = (idx >> 2) & 1;
            float x0 = ix? cx : b.bminx, x1 = ix? b.bmaxx : cx;
            float y0 = iy? cy : b.bminy, y1 = iy? b.bmaxy : cy;
            float z0 = iz? cz : b.bminz, z1 = iz? b.bmaxz : cz;
            Bucket nb{ x0,y0,z0,x1,y1,z1, {}, (b.bx<<1)|ix, (b.by<<1)|iy, (b.bz<<1)|iz, b.level+1 };
            out.push_back(std::move(nb));
        }
        for (const auto &p : b.pts) {
            int ix = (p.x >= cx); int iy = (p.y >= cy); int iz = (p.z >= cz);
            int idx = (iz<<2) | (iy<<1) | ix;
            out[idx].pts.push_back(p);
        }
        return out;
    };

    // BFS/队列自适应细分
    std::vector<Bucket> final_buckets; final_buckets.reserve(128);
    std::vector<Bucket> queue; queue.reserve(128); queue.push_back(std::move(buckets[0])); buckets.clear();
    while (!queue.empty()) {
        Bucket b = std::move(queue.back()); queue.pop_back();
        if ((int)b.pts.size() <= bucket_max_points || b.level >= max_inner_levels) {
            final_buckets.push_back(std::move(b));
        } else {
            auto children = subdivide(b);
            for (auto &ch : children) if (!ch.pts.empty()) queue.push_back(std::move(ch));
        }
    }

    // 打开文件写入：总数 + 每桶（按序）写点，记录偏移
    std::ofstream ofs(file_path, std::ios::binary);
    if (!ofs.is_open()) {
        elog(ERROR, "Failed to write leaf file: %s", file_path.string().c_str());
    }
    uint32_t total = (uint32_t)points.size();
    ofs.write(reinterpret_cast<const char*>(&total), sizeof(uint32_t));

    // 写桶与 kd 叶索引
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(ERROR, "SPI_connect failed");
    }

    int total_kd_leaves = 0;
    out_bucket_count = 0;
    for (auto &bk : final_buckets) {
        int bx = bk.bx, by = bk.by, bz = bk.bz;
        auto &pts = bk.pts;
        // 记录桶起始偏移
        uint64_t bucket_offset = (uint64_t)ofs.tellp();

        // 构建真正的 kd-tree：递归按最大跨度轴二分，直到叶子数量<=阈值
        struct KDLeaf { uint64_t offset; uint32_t count; float minx,miny,minz,maxx,maxy,maxz; };
        std::vector<KDLeaf> kd_leaves;
        kd_leaves.reserve(std::max<size_t>(1, pts.size() / (kd_leaf_max_points ? kd_leaf_max_points : 1)));

        std::function<void(size_t,size_t)> build_kd = [&](size_t s, size_t e){
            if (s >= e) return;
            // 统计 bbox
            float kminx=+INFINITY,kminy=+INFINITY,kminz=+INFINITY,kmaxx=-INFINITY,kmaxy=-INFINITY,kmaxz=-INFINITY;
            for (size_t i=s;i<e;++i) { auto &p=pts[i]; update_bbox(p.x,p.y,p.z,kminx,kminy,kminz,kmaxx,kmaxy,kmaxz); }
            size_t n = e - s;
            if ((int)n <= kd_leaf_max_points) {
                // 写该叶
                uint64_t off = (uint64_t)ofs.tellp();
                for (size_t i=s;i<e;++i) {
                    auto &p = pts[i];
                    ofs.write(reinterpret_cast<const char*>(&p.x), sizeof(float));
                    ofs.write(reinterpret_cast<const char*>(&p.y), sizeof(float));
                    ofs.write(reinterpret_cast<const char*>(&p.z), sizeof(float));
                    ofs.write(reinterpret_cast<const char*>(&p.fid), sizeof(uint32_t));
                    ofs.write(reinterpret_cast<const char*>(&p.row), sizeof(uint32_t));
                }
                kd_leaves.push_back(KDLeaf{off, (uint32_t)n, kminx,kminy,kminz,kmaxx,kmaxy,kmaxz});
                return;
            }
            // 选择最大跨度轴并按中位数划分
            float sx = kmaxx - kminx, sy = kmaxy - kminy, sz = kmaxz - kminz;
            int axis = (sx>=sy && sx>=sz)?0:((sy>=sz)?1:2);
            size_t mid = s + n/2;
            auto proj = [&](const BuildPoint &p){ return axis==0 ? p.x : (axis==1 ? p.y : p.z); };
            std::nth_element(pts.begin()+s, pts.begin()+mid, pts.begin()+e,
                             [&](const BuildPoint&a,const BuildPoint&b){ return proj(a) < proj(b); });
            build_kd(s, mid);
            build_kd(mid, e);
        };

        build_kd(0, pts.size());
        total_kd_leaves += (int)kd_leaves.size();

        // 插入桶行
        float bminx=bk.bminx,bminy=bk.bminy,bminz=bk.bminz,bmaxx=bk.bmaxx,bmaxy=bk.bmaxy,bmaxz=bk.bmaxz;
        char bsql[1024];
        snprintf(bsql, sizeof(bsql),
                 "INSERT INTO trace_leaf_bucket(dataset_path,leaf_prefix,leaf_level,bx,by,bz,level,data_offset,count,file_path,minx,miny,minz,maxx,maxy,maxz) "
                 "VALUES (%s, %lld, %d, %d, %d, %d, %d, %lld, %u, %s, %s, %s, %s, %s, %s, %s)",
                 sql_quote_literal(dataset_path).c_str(), (long long)leaf_prefix, leaf_level,
                 bx, by, bz, bk.level,
                 (long long)bucket_offset, (unsigned)pts.size(), sql_quote_literal(file_path.string()).c_str(),
                 format_float_for_sql(bminx).c_str(), format_float_for_sql(bminy).c_str(), format_float_for_sql(bminz).c_str(),
                 format_float_for_sql(bmaxx).c_str(), format_float_for_sql(bmaxy).c_str(), format_float_for_sql(bmaxz).c_str());
        SPI_execute(bsql, false, 0);
        out_bucket_count += 1;

        // 插入 kd 叶行
        for (int ki=0; ki<(int)kd_leaves.size(); ++ki) {
            const auto &kl = kd_leaves[ki];
            char ksql[1024];
            snprintf(ksql, sizeof(ksql),
                     "INSERT INTO trace_bucket_kdleaf(dataset_path,leaf_prefix,leaf_level,bx,by,bz,level,kd_idx,data_offset,count,file_path,minx,miny,minz,maxx,maxy,maxz) "
                     "VALUES (%s, %lld, %d, %d, %d, %d, %d, %d, %lld, %u, %s, %s, %s, %s, %s, %s, %s)",
                     sql_quote_literal(dataset_path).c_str(), (long long)leaf_prefix, leaf_level,
                     bx, by, bz, bk.level, ki, (long long)kl.offset, (unsigned)kl.count, sql_quote_literal(file_path.string()).c_str(),
                     format_float_for_sql(kl.minx).c_str(), format_float_for_sql(kl.miny).c_str(), format_float_for_sql(kl.minz).c_str(),
                     format_float_for_sql(kl.maxx).c_str(), format_float_for_sql(kl.maxy).c_str(), format_float_for_sql(kl.maxz).c_str());
            SPI_execute(ksql, false, 0);
        }
    }

    SPI_finish();
    ofs.close();
    return total_kd_leaves;
}

std::vector<LeafMeta> db_query_leaves_intersecting(const std::string &dataset_path, const SimpleBounds &b)
{
    std::vector<LeafMeta> metas;
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(ERROR, "SPI_connect failed");
    }
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT file_path, point_count, minx, miny, minz, maxx, maxy, maxz "
             "FROM trace_octree_leaf WHERE dataset_path=%s AND "
             "maxx >= %f AND minx <= %f AND maxy >= %f AND miny <= %f AND maxz >= %f AND minz <= %f",
             sql_quote_literal(dataset_path).c_str(),
             b.min_x, b.max_x, b.min_y, b.max_y, b.min_z, b.max_z);
    int rc = SPI_execute(sql, true, 0);
    if (rc != SPI_OK_SELECT) {
        SPI_finish();
        elog(ERROR, "SPI_execute select failed");
    }
    SPITupleTable *tuptable = SPI_tuptable;
    TupleDesc tupdesc = tuptable->tupdesc;
    uint64 nrows = SPI_processed;
    metas.reserve((size_t)nrows);
    for (uint64 i = 0; i < nrows; ++i) {
        HeapTuple tuple = tuptable->vals[i];
        const char *v_file = SPI_getvalue(tuple, tupdesc, 1);
        const char *v_cnt  = SPI_getvalue(tuple, tupdesc, 2);
        const char *v_minx = SPI_getvalue(tuple, tupdesc, 3);
        const char *v_miny = SPI_getvalue(tuple, tupdesc, 4);
        const char *v_minz = SPI_getvalue(tuple, tupdesc, 5);
        const char *v_maxx = SPI_getvalue(tuple, tupdesc, 6);
        const char *v_maxy = SPI_getvalue(tuple, tupdesc, 7);
        const char *v_maxz = SPI_getvalue(tuple, tupdesc, 8);
        LeafMeta m{};
        m.file_path = v_file ? v_file : "";
        m.point_count = (uint32_t)std::strtoul(v_cnt ? v_cnt : "0", nullptr, 10);
        m.minx = v_minx ? std::strtof(v_minx, nullptr) : 0.0f;
        m.miny = v_miny ? std::strtof(v_miny, nullptr) : 0.0f;
        m.minz = v_minz ? std::strtof(v_minz, nullptr) : 0.0f;
        m.maxx = v_maxx ? std::strtof(v_maxx, nullptr) : 0.0f;
        m.maxy = v_maxy ? std::strtof(v_maxy, nullptr) : 0.0f;
        m.maxz = v_maxz ? std::strtof(v_maxz, nullptr) : 0.0f;
        metas.push_back(std::move(m));
    }
    SPI_finish();
    return metas;
}

std::vector<LeafMeta> db_query_all_leaves(const std::string &dataset_path)
{
    std::vector<LeafMeta> metas;
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(ERROR, "SPI_connect failed");
    }
    std::string sql = std::string("SELECT file_path, point_count, minx, miny, minz, maxx, maxy, maxz FROM trace_octree_leaf WHERE dataset_path=") + sql_quote_literal(dataset_path);
    int rc = SPI_execute(sql.c_str(), true, 0);
    if (rc != SPI_OK_SELECT) {
        SPI_finish();
        elog(ERROR, "SPI_execute select failed");
    }
    SPITupleTable *tuptable = SPI_tuptable;
    TupleDesc tupdesc = tuptable->tupdesc;
    uint64 nrows = SPI_processed;
    metas.reserve((size_t)nrows);
    for (uint64 i = 0; i < nrows; ++i) {
        HeapTuple tuple = tuptable->vals[i];
        const char *v_file = SPI_getvalue(tuple, tupdesc, 1);
        const char *v_cnt  = SPI_getvalue(tuple, tupdesc, 2);
        const char *v_minx = SPI_getvalue(tuple, tupdesc, 3);
        const char *v_miny = SPI_getvalue(tuple, tupdesc, 4);
        const char *v_minz = SPI_getvalue(tuple, tupdesc, 5);
        const char *v_maxx = SPI_getvalue(tuple, tupdesc, 6);
        const char *v_maxy = SPI_getvalue(tuple, tupdesc, 7);
        const char *v_maxz = SPI_getvalue(tuple, tupdesc, 8);
        LeafMeta m{};
        m.file_path = v_file ? v_file : "";
        m.point_count = (uint32_t)std::strtoul(v_cnt ? v_cnt : "0", nullptr, 10);
        m.minx = v_minx ? std::strtof(v_minx, nullptr) : 0.0f;
        m.miny = v_miny ? std::strtof(v_miny, nullptr) : 0.0f;
        m.minz = v_minz ? std::strtof(v_minz, nullptr) : 0.0f;
        m.maxx = v_maxx ? std::strtof(v_maxx, nullptr) : 0.0f;
        m.maxy = v_maxy ? std::strtof(v_maxy, nullptr) : 0.0f;
        m.maxz = v_maxz ? std::strtof(v_maxz, nullptr) : 0.0f;
        metas.push_back(std::move(m));
    }
    SPI_finish();
    return metas;
}

float bbox_point_min_dist2(const LeafMeta &m, float x, float y, float z)
{
    float dx = 0.0f; if (x < m.minx) dx = m.minx - x; else if (x > m.maxx) dx = x - m.maxx;
    float dy = 0.0f; if (y < m.miny) dy = m.miny - y; else if (y > m.maxy) dy = y - m.maxy;
    float dz = 0.0f; if (z < m.minz) dz = m.minz - z; else if (z > m.maxz) dz = z - m.maxz;
    return dx*dx + dy*dy + dz*dz;
}

void read_leaf_points_filter_bounds(const LeafMeta &m, const SimpleBounds &b, int data_type_mask, std::vector<SimplePoint> &out)
{
    if ((data_type_mask & TRACE_TYPE_POINTCLOUD) == 0) return;
    std::ifstream in(m.file_path, std::ios::binary);
    if (!in.is_open()) {
        elog(WARNING, "Failed to open leaf file: %s", m.file_path.c_str());
        return;
    }
    uint32_t count = 0; in.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
    for (uint32_t i = 0; i < count; ++i) {
        float x,y,z; uint32_t fid,row;
        in.read(reinterpret_cast<char*>(&x), sizeof(float));
        in.read(reinterpret_cast<char*>(&y), sizeof(float));
        in.read(reinterpret_cast<char*>(&z), sizeof(float));
        in.read(reinterpret_cast<char*>(&fid), sizeof(uint32_t));
        in.read(reinterpret_cast<char*>(&row), sizeof(uint32_t));
        if (!in) break;
        if (x < b.min_x || x > b.max_x) continue;
        if (y < b.min_y || y > b.max_y) continue;
        if (z < b.min_z || z > b.max_z) continue;
        SimplePoint p{}; p.x=x; p.y=y; p.z=z; p.time=0.0f;
        p.data_type = TRACE_TYPE_POINTCLOUD; p.fid=(int)fid; p.pid=(int)row; p.foreign_key=0;
        out.push_back(p);
    }
}

void read_leaf_points_update_knn(const LeafMeta &m, const SimplePoint &c, int k, std::priority_queue<std::pair<float,KnnCand>> &heap)
{
    std::ifstream in(m.file_path, std::ios::binary);
    if (!in.is_open()) {
        elog(WARNING, "Failed to open leaf file: %s", m.file_path.c_str());
        return;
    }
    uint32_t count = 0; in.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
    for (uint32_t i = 0; i < count; ++i) {
        KnnCand cand{}; uint32_t fid,row;
        in.read(reinterpret_cast<char*>(&cand.x), sizeof(float));
        in.read(reinterpret_cast<char*>(&cand.y), sizeof(float));
        in.read(reinterpret_cast<char*>(&cand.z), sizeof(float));
        in.read(reinterpret_cast<char*>(&fid), sizeof(uint32_t));
        in.read(reinterpret_cast<char*>(&row), sizeof(uint32_t));
        if (!in) break;
        cand.fid = fid; cand.row = row;
        float dx=cand.x-c.x, dy=cand.y-c.y, dz=cand.z-c.z;
        cand.dist2 = dx*dx+dy*dy+dz*dz;
        if ((int)heap.size() < k) {
            heap.emplace(cand.dist2, cand);
        } else if (cand.dist2 < heap.top().first) {
            heap.pop(); heap.emplace(cand.dist2, cand);
        }
    }
}

std::vector<BucketMeta> db_query_buckets_intersecting(const std::string &dataset_path,
                                                      const SimpleBounds &b)
{
    std::vector<BucketMeta> metas;
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(ERROR, "SPI_connect failed");
    }
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT file_path, bx,by,bz, level, leaf_prefix, leaf_level, data_offset, count, minx,miny,minz,maxx,maxy,maxz "
             "FROM trace_leaf_bucket WHERE dataset_path=%s AND "
             "maxx >= %f AND minx <= %f AND maxy >= %f AND miny <= %f AND maxz >= %f AND minz <= %f",
             sql_quote_literal(dataset_path).c_str(), b.min_x, b.max_x, b.min_y, b.max_y, b.min_z, b.max_z);
    int rc = SPI_execute(sql, true, 0);
    if (rc != SPI_OK_SELECT) { SPI_finish(); elog(ERROR, "SPI_execute select failed"); }
    SPITupleTable *tuptable = SPI_tuptable; TupleDesc tupdesc = tuptable->tupdesc; uint64 nrows = SPI_processed;
    metas.reserve((size_t)nrows);
    for (uint64 i=0;i<nrows;++i){ HeapTuple t=tuptable->vals[i]; BucketMeta m{}; const char *v;
        m.file_path = (v=SPI_getvalue(t,tupdesc,1))?v:"";
        m.bx = std::atoi((v=SPI_getvalue(t,tupdesc,2))?v:"0");
        m.by = std::atoi((v=SPI_getvalue(t,tupdesc,3))?v:"0");
        m.bz = std::atoi((v=SPI_getvalue(t,tupdesc,4))?v:"0");
        m.level = std::atoi((v=SPI_getvalue(t,tupdesc,5))?v:"0");
        m.leaf_prefix = std::strtoull((v=SPI_getvalue(t,tupdesc,6))?v:"0", nullptr, 10);
        m.leaf_level = std::atoi((v=SPI_getvalue(t,tupdesc,7))?v:"0");
        m.offset = std::strtoull((v=SPI_getvalue(t,tupdesc,8))?v:"0", nullptr, 10);
        m.count = (uint32_t)std::strtoul((v=SPI_getvalue(t,tupdesc,9))?v:"0", nullptr, 10);
        m.minx = (v=SPI_getvalue(t,tupdesc,10))?std::strtof(v,nullptr):0.0f;
        m.miny = (v=SPI_getvalue(t,tupdesc,11))?std::strtof(v,nullptr):0.0f;
        m.minz = (v=SPI_getvalue(t,tupdesc,12))?std::strtof(v,nullptr):0.0f;
        m.maxx = (v=SPI_getvalue(t,tupdesc,13))?std::strtof(v,nullptr):0.0f;
        m.maxy = (v=SPI_getvalue(t,tupdesc,14))?std::strtof(v,nullptr):0.0f;
        m.maxz = (v=SPI_getvalue(t,tupdesc,15))?std::strtof(v,nullptr):0.0f;
        metas.push_back(std::move(m)); }
    SPI_finish();
    return metas;
}

std::vector<KdLeafMeta> db_query_all_kdleaves(const std::string &dataset_path)
{
    std::vector<KdLeafMeta> metas;
    if (SPI_connect() != SPI_OK_CONNECT) { elog(ERROR, "SPI_connect failed"); }
    std::string sql = std::string("SELECT file_path, bx,by,bz, level, leaf_prefix, leaf_level, data_offset, count, minx,miny,minz,maxx,maxy,maxz FROM trace_bucket_kdleaf WHERE dataset_path=") + sql_quote_literal(dataset_path);
    int rc = SPI_execute(sql.c_str(), true, 0);
    if (rc != SPI_OK_SELECT) { SPI_finish(); elog(ERROR, "SPI_execute select failed"); }
    SPITupleTable *tuptable = SPI_tuptable; TupleDesc tupdesc = tuptable->tupdesc; uint64 nrows = SPI_processed;
    metas.reserve((size_t)nrows);
    for (uint64 i=0;i<nrows;++i){ HeapTuple t=tuptable->vals[i]; KdLeafMeta m{}; const char *v;
        m.file_path = (v=SPI_getvalue(t,tupdesc,1))?v:"";
        m.bx = std::atoi((v=SPI_getvalue(t,tupdesc,2))?v:"0");
        m.by = std::atoi((v=SPI_getvalue(t,tupdesc,3))?v:"0");
        m.bz = std::atoi((v=SPI_getvalue(t,tupdesc,4))?v:"0");
        // Skip level column (index 5) - not used in KdLeafMeta
        m.leaf_prefix = std::strtoull((v=SPI_getvalue(t,tupdesc,6))?v:"0", nullptr, 10);
        m.leaf_level = std::atoi((v=SPI_getvalue(t,tupdesc,7))?v:"0");
        m.offset = std::strtoull((v=SPI_getvalue(t,tupdesc,8))?v:"0", nullptr, 10);
        m.count = (uint32_t)std::strtoul((v=SPI_getvalue(t,tupdesc,9))?v:"0", nullptr, 10);
        m.minx = (v=SPI_getvalue(t,tupdesc,10))?std::strtof(v,nullptr):0.0f;
        m.miny = (v=SPI_getvalue(t,tupdesc,11))?std::strtof(v,nullptr):0.0f;
        m.minz = (v=SPI_getvalue(t,tupdesc,12))?std::strtof(v,nullptr):0.0f;
        m.maxx = (v=SPI_getvalue(t,tupdesc,13))?std::strtof(v,nullptr):0.0f;
        m.maxy = (v=SPI_getvalue(t,tupdesc,14))?std::strtof(v,nullptr):0.0f;
        m.maxz = (v=SPI_getvalue(t,tupdesc,15))?std::strtof(v,nullptr):0.0f;
        metas.push_back(std::move(m)); }
    SPI_finish(); return metas;
}

void read_points_block_filter_bounds(const std::string &file_path, uint64_t offset, uint32_t count,
                                     const SimpleBounds &b, int data_type_mask,
                                     std::vector<SimplePoint> &out)
{
    if ((data_type_mask & TRACE_TYPE_POINTCLOUD) == 0) return;
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) { elog(WARNING, "Failed to open block file: %s", file_path.c_str()); return; }
    in.seekg(offset, std::ios::beg);
    for (uint32_t i=0;i<count;++i){ float x,y,z; uint32_t fid,row; in.read(reinterpret_cast<char*>(&x),4); in.read(reinterpret_cast<char*>(&y),4); in.read(reinterpret_cast<char*>(&z),4); in.read(reinterpret_cast<char*>(&fid),4); in.read(reinterpret_cast<char*>(&row),4); if (!in) break; if (x<b.min_x||x>b.max_x||y<b.min_y||y>b.max_y||z<b.min_z||z>b.max_z) continue; SimplePoint p{}; p.x=x;p.y=y;p.z=z;p.time=0.0f;p.data_type=TRACE_TYPE_POINTCLOUD;p.fid=(int)fid;p.pid=(int)row;p.foreign_key=0; out.push_back(p);} }

void read_points_block_update_knn(const std::string &file_path, uint64_t offset, uint32_t count,
                                  const SimplePoint &c, int k,
                                  std::priority_queue<std::pair<float,KnnCand>> &heap)
{
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) { elog(WARNING, "Failed to open block file: %s", file_path.c_str()); return; }
    in.seekg(offset, std::ios::beg);
    for (uint32_t i=0;i<count;++i){
        KnnCand cand{}; uint32_t fid,row;
        in.read(reinterpret_cast<char*>(&cand.x),4);
        in.read(reinterpret_cast<char*>(&cand.y),4);
        in.read(reinterpret_cast<char*>(&cand.z),4);
        in.read(reinterpret_cast<char*>(&fid),4);
        in.read(reinterpret_cast<char*>(&row),4);
        if (!in) break;
        cand.fid=fid; cand.row=row;
        // Distance in meters squared using lon/lat degrees and z in meters
        double d2m = distance_meters2_3d((double)c.x, (double)c.y, (double)c.z,
                                         (double)cand.x, (double)cand.y, (double)cand.z);
        cand.dist2 = (float)d2m;
        if ((int)heap.size()<k) heap.emplace(cand.dist2,cand);
        else if (cand.dist2<heap.top().first){ heap.pop(); heap.emplace(cand.dist2,cand);} }
}

void read_points_block_filter_radius(const std::string &file_path, uint64_t offset, uint32_t count,
                                     const SimplePoint &c, float radius,
                                     int data_type_mask,
                                     std::vector<SimplePoint> &out)
{
    if ((data_type_mask & TRACE_TYPE_POINTCLOUD) == 0) return;
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) { elog(WARNING, "Failed to open block file: %s", file_path.c_str()); return; }
    in.seekg(offset, std::ios::beg);
    // Interpret x=lon(deg), y=lat(deg), z=height(m). Compute 3D distance in meters.
    const double R = 6371008.8; // mean Earth radius in meters
    const double lat0 = (double)c.y * M_PI / 180.0;
    const double lon0 = (double)c.x * M_PI / 180.0;
    const double r2 = (double)radius * (double)radius;
    for (uint32_t i=0;i<count;++i){
        float x,y,z; uint32_t fid,row;
        in.read(reinterpret_cast<char*>(&x),4);
        in.read(reinterpret_cast<char*>(&y),4);
        in.read(reinterpret_cast<char*>(&z),4);
        in.read(reinterpret_cast<char*>(&fid),4);
        in.read(reinterpret_cast<char*>(&row),4);
        if (!in) break;
        // horizontal great-circle distance using haversine
        double lat = (double)y * M_PI / 180.0;
        double lon = (double)x * M_PI / 180.0;
        double dlat = lat - lat0;
        double dlon = lon - lon0;
        double sin_dlat2 = std::sin(dlat*0.5);
        double sin_dlon2 = std::sin(dlon*0.5);
        double a = sin_dlat2*sin_dlat2 + std::cos(lat0)*std::cos(lat)*sin_dlon2*sin_dlon2;
        double cang = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
        double horizontal_m = R * cang;
        double dz = (double)z - (double)c.z; // z already meters
        double d2 = horizontal_m*horizontal_m + dz*dz;
        if (d2 > r2) continue;
        SimplePoint p{}; p.x=x;p.y=y;p.z=z;p.time=0.0f;p.data_type=TRACE_TYPE_POINTCLOUD;p.fid=(int)fid;p.pid=(int)row;p.foreign_key=0; out.push_back(p);
    }
}


