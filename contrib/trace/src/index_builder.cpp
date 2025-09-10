#include "../include/safe_header.h"

#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cmath>

#include "../include/index_builder.h"
#include "../include/index_storage.h"
#include "../include/morton_utils.h"

extern SimpleBounds global_bounds; // used for compatibility

// utilities from trace.cpp mirrored here (compact)

IndexBuilder::IndexBuilder(const std::string &dataset_path,
                           const SimpleBounds &global_bounds,
                           const std::vector<std::string> &source_files,
                           const Params &params)
    : dataset_path_(dataset_path), bounds_(global_bounds), files_(source_files), params_(params) {}

uint64_t IndexBuilder::morton3_(uint32_t xi, uint32_t yi, uint32_t zi){ return morton3d_21(xi, yi, zi); }

uint64_t IndexBuilder::prefix_(uint64_t key, int level){ return morton_prefix(key, level); }

void IndexBuilder::deinterleave_(uint64_t pref, int level, uint32_t &xi, uint32_t &yi, uint32_t &zi){ deinterleave_prefix(pref, level, xi, yi, zi); }

uint32_t IndexBuilder::q21_(float v, float vmin, float vmax){ return quantize21(v, vmin, vmax); }

void IndexBuilder::scan_points_(){
    postings_.clear();
    for (uint32_t fid=0; fid<files_.size(); ++fid){
        std::ifstream in(files_[fid]); if (!in.is_open()) { elog(WARNING, "IndexBuilder open failed: %s", files_[fid].c_str()); continue; }
        std::string line; uint32_t row=0; while (std::getline(in,line)){
            if (line.empty()) { ++row; continue; }
            size_t p0=0,p1=line.find(','); if (p1==std::string::npos){ ++row; continue; } float x=std::strtof(line.substr(p0,p1-p0).c_str(), nullptr);
            p0=p1+1; p1=line.find(',',p0); if (p1==std::string::npos){ ++row; continue; } float y=std::strtof(line.substr(p0,p1-p0).c_str(), nullptr);
            p0=p1+1; p1=line.find(',',p0); if (p1==std::string::npos){ ++row; continue; } float z=std::strtof(line.substr(p0,p1-p0).c_str(), nullptr);
            uint32_t xi=q21_(x,bounds_.min_x,bounds_.max_x), yi=q21_(y,bounds_.min_y,bounds_.max_y), zi=q21_(z,bounds_.min_z,bounds_.max_z);
            postings_.push_back(Posting{ morton3_(xi,yi,zi), x,y,z, fid, row }); ++row;
        }
    }
}

void IndexBuilder::sort_by_morton_(){
    std::sort(postings_.begin(), postings_.end(), [](const Posting&a,const Posting&b){ return a.morton<b.morton; });
}

void IndexBuilder::bulkload_octree_(){
    nodes_.clear();
    struct Range { uint32_t s,e; uint64_t pref; int level; int parent; int child_slot; };
    std::vector<Range> cur, nxt; cur.push_back(Range{0u, (uint32_t)postings_.size(), 0ull, 0, -1, -1});
    while(!cur.empty()){
        nxt.clear();
        for (auto &r : cur){
            Node n{}; n.prefix=r.pref; n.level=r.level; n.start=r.s; n.end=r.e; deinterleave_(r.pref, r.level, n.xi, n.yi, n.zi);
            bool leaf = ((r.e - r.s) <= (uint32_t)params_.max_points_per_leaf) || (r.level >= params_.octree_max_level);
            n.is_leaf = leaf; int my = (int)nodes_.size(); nodes_.push_back(n);
            if (leaf) continue;
            uint32_t i=r.s; while (i<r.e){ uint64_t pfx=prefix_(postings_[i].morton, r.level+1); uint32_t j=i+1; while (j<r.e && prefix_(postings_[j].morton, r.level+1)==pfx) ++j; int slot=(int)(pfx & 0x7ULL); nxt.push_back(Range{i,j,pfx,r.level+1,my,slot}); i=j; }
        }
        cur.swap(nxt);
    }
}

void IndexBuilder::persist_leaves_(){
    persist_prepare_dataset(dataset_path_);
    for (const auto &n : nodes_) if (n.is_leaf){
        std::vector<BuildPoint> pts; pts.reserve(n.end-n.start);
        for (uint32_t i=n.start;i<n.end;++i){ const auto &e=postings_[i]; pts.push_back(BuildPoint{e.x,e.y,e.z,e.fid,e.row}); }
        float minx, miny, minz, maxx, maxy, maxz;
        // reuse global dequantize not needed; bbox from quantized cells: approximate by proportion of bounds using level
        // simpler: compute from points to be exact
        minx=+INFINITY; miny=+INFINITY; minz=+INFINITY; maxx=-INFINITY; maxy=-INFINITY; maxz=-INFINITY;
        for (auto &p: pts){ if (p.x<minx) minx=p.x; if (p.y<miny) miny=p.y; if (p.z<minz) minz=p.z; if (p.x>maxx) maxx=p.x; if (p.y>maxy) maxy=p.y; if (p.z>maxz) maxz=p.z; }
        persist_leaf_index(dataset_path_, n.prefix, n.level, n.xi, n.yi, n.zi,
                           minx,miny,minz,maxx,maxy,maxz, pts,
                           params_.bucket_max_points, params_.max_inner_levels, params_.kd_leaf_max_points);
    }
}

void IndexBuilder::build_all(){
    scan_points_();
    sort_by_morton_();
    bulkload_octree_();
    persist_leaves_();
    // release memory implicitly when object goes out of scope
}


